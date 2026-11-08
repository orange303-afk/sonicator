#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <sqlite3.h>
using namespace sonicator;
TEST_CASE("Folder queries include every descendant but not sibling prefixes", "[database][folders]")
{
    TestDirectory t; Database db(t.dir.getChildFile("folders.sqlite")); SampleRepository repo(db); repo.createTables();
    for (auto path : {"/lib/Drums/k.wav", "/lib/Drums/deep/more/s.wav", "/lib/Drums2/no.wav", "/lib/other.wav"})
    {
        SampleRecord r; r.filePath = path; r.fileName = juce::File(path).getFileName(); repo.upsert(r);
    }
    REQUIRE(repo.countInFolder("/lib/Drums") == 2);
    REQUIRE(repo.pageInFolder("/lib/Drums",1,1).size() == 1);
    REQUIRE(repo.countInFolder("/absent") == 0);
    REQUIRE(repo.countInFolder({}) == 4);
}
TEST_CASE("Repository persists Unicode paths, metrics, and upserts without losing identity", "[database]")
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-db", ".sqlite");
    int64_t id = 0;
    {
        Database db(file);
        SampleRepository repo(db);
        repo.createTables();
        SampleRecord r;
        r.filePath = juce::String::fromUTF8("/тест/音/🎵.wav"); r.fileName = juce::String::fromUTF8("🎵.wav");
        r.fileHash = "same-content"; r.duration = 1.25;
        r.spectralRolloff = 1234.0; r.spectralFlatness = 0.17;
        r.version = basicFeaturesVersion;
        id = repo.upsert(r);
        REQUIRE(id > 0);
        REQUIRE(repo.count() == 1);
        r.duration = 2.5;
        REQUIRE(repo.upsert(r) == id);
        REQUIRE(repo.count() == 1);
        r.filePath = "/another/copy.wav";
        repo.upsert(r);
        REQUIRE(repo.countHash("same-content") == 2);
        repo.addRoot(juce::String::fromUTF8("/тест")); repo.addRoot(juce::String::fromUTF8("/тест"));
        REQUIRE(repo.roots().size() == 1);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        auto r = repo.findByPath(juce::String::fromUTF8("/тест/音/🎵.wav"));
        REQUIRE(r.has_value());
        REQUIRE(r->filePath.toStdString() == "/тест/音/🎵.wav");
        REQUIRE(r->id == id);
        REQUIRE(r->duration == Catch::Approx(2.5));
        REQUIRE(r->spectralRolloff == Catch::Approx(1234));
        REQUIRE(r->spectralFlatness == Catch::Approx(0.17));
        REQUIRE(r->version == basicFeaturesVersion);
        REQUIRE(repo.page(1, 0).size() == 1);
        REQUIRE(repo.page(1, 2).empty());
        REQUIRE_FALSE(repo.findByPath("absent"));
    }
    REQUIRE(file.deleteFile());
}
namespace {
void execRaw(sqlite3* db, const char* sql)
{
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
}
int rawScalar(const juce::File& file, const char* sql)
{
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(raw, sql, -1, &s, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(s) == SQLITE_ROW);
    const int value = sqlite3_column_int(s, 0);
    sqlite3_finalize(s); sqlite3_close(raw);
    return value;
}
}
TEST_CASE("Schema v1 migrates in place, keeps rows, and backs up once", "[database][migration]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("legacy.sqlite");
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        execRaw(raw, "CREATE TABLE samples (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,"
                     "name TEXT NOT NULL, hash TEXT NOT NULL, format TEXT NOT NULL, error TEXT NOT NULL,"
                     "size INTEGER, modified INTEGER, analyzed INTEGER, duration REAL, rate REAL, bits INTEGER,"
                     "channels INTEGER, version INTEGER, peak REAL, rms REAL, centroid REAL, rolloff REAL, flatness REAL, missing INTEGER);");
        execRaw(raw, "INSERT INTO samples(path,name,hash,format,error,version) VALUES('/a.wav','a.wav','h1','WAV','',1),"
                     "('/b.wav','b.wav','h2','WAV','',1),('/c.wav','c.wav','h3','WAV','',2);");
        execRaw(raw, "PRAGMA user_version=1;");
        sqlite3_close(raw);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.count() == 3);
        REQUIRE(rawScalar(file, "SELECT COUNT(*) FROM samples WHERE version=1") == 3);
    }
    const auto backups = t.dir.findChildFiles(juce::File::findFiles, false, "legacy.sqlite.backup-*");
    REQUIRE(backups.size() == 1);
    REQUIRE(rawScalar(backups[0], "SELECT COUNT(*) FROM samples") == 3);
    REQUIRE(rawScalar(backups[0], "PRAGMA user_version") == 1);
    REQUIRE(rawScalar(file, "PRAGMA user_version") == 10);
    REQUIRE(rawScalar(file, "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='samples_name_id'") == 1);
    { Database db(file); SampleRepository repo(db); repo.createTables(); REQUIRE(repo.count() == 3); }
    REQUIRE(t.dir.findChildFiles(juce::File::findFiles, false, "legacy.sqlite.backup-*").size() == 1);
}
TEST_CASE("Path stream walks by primary key and marks missing in batches", "[database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("c.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/x/c.wav"; r.fileName = "c.wav"; r.fileHash = "hc";
    auto idC = repo.upsert(r);
    REQUIRE(idC > 0);
    r.filePath = "/x/a.wav"; r.fileName = "a.wav"; r.fileHash = "ha"; auto idA = repo.upsert(r);
    r.filePath = "/x/b.wav"; r.fileName = "b.wav"; r.fileHash = "hb"; auto idB = repo.upsert(r);
    auto first = repo.pathsAfterId(0, 2);
    REQUIRE(first.size() == 2);
    REQUIRE(first[0].id < first[1].id);
    auto second = repo.pathsAfterId(first.back().id, 2);
    REQUIRE(second.size() == 1);
    REQUIRE(second[0].path == "/x/b.wav");
    repo.setMissing({idA, idB});
    REQUIRE(repo.findByPath("/x/a.wav")->missing);
    REQUIRE(repo.findByPath("/x/b.wav")->missing);
    REQUIRE_FALSE(repo.findByPath("/x/c.wav")->missing);
    repo.setMissing({});
    REQUIRE(repo.findByPath("/x/a.wav")->missing);
}
TEST_CASE("Database open failure is reported", "[database]")
{
    REQUIRE_THROWS(Database(juce::File::getSpecialLocation(juce::File::tempDirectory)));
}
namespace {
SampleRecord sample(const juce::String& path, const juce::String& name, double duration = 1.0)
{
    SampleRecord r; r.filePath = path; r.fileName = name; r.fileHash = "h"; r.format = "WAV";
    r.duration = duration; r.version = basicFeaturesVersion; return r;
}
}
TEST_CASE("Whole-catalog search is case-insensitive, token-based and escapes wildcards", "[database][search]")
{
    TestDirectory t; Database db(t.dir.getChildFile("search.sqlite")); SampleRepository repo(db); repo.createTables();
    const auto kick1 = repo.upsert(sample("/lib/Drums/Kick_01.wav", "Kick_01.wav", 2.0));
    repo.upsert(sample("/lib/Drums/Kick_02.wav", "Kick_02.wav", 1.0));
    repo.upsert(sample("/lib/Bass/Sub_Bass.wav", "Sub_Bass.wav"));
    repo.upsert(sample(juce::String::fromUTF8("/Тест/Файлы/Бас_Тест.wav"), juce::String::fromUTF8("Бас_Тест.wav")));
    repo.upsert(sample("/lib/FX/100%_noise.wav", "100%_noise.wav"));
    repo.upsert(sample("/lib/FX/under_score.wav", "under_score.wav"));
    repo.upsert(sample(juce::String::fromUTF8("/Greek/ΔΟΚΙΜΗ.wav"), juce::String::fromUTF8("ΔΟΚΙΜΗ.wav")));

    LibraryFilter f;
    f.text = "kick";
    auto page = repo.query(f);
    REQUIRE(page.total == 2);
    REQUIRE(page.samples.size() == 2);
    REQUIRE(page.samples.front().fileName.compareIgnoreCase("Kick_01.wav") == 0);
    f.text = "KICK 02"; REQUIRE(repo.query(f).total == 1);
    f.text = "kick bass"; REQUIRE(repo.query(f).total == 0);
    f.text = juce::String::fromUTF8("тест"); REQUIRE(repo.query(f).total == 1);
    f.text = juce::String::fromUTF8("δοκιμη"); REQUIRE(repo.query(f).total == 1);
    f.text = "%"; REQUIRE(repo.query(f).total == 1);
    f.text = "_01"; REQUIRE(repo.query(f).total == 1);
    f.text = "score"; REQUIRE(repo.query(f).total == 1);

    f.text = "kick"; f.folder = "/lib/Drums"; REQUIRE(repo.query(f).total == 2);
    f.folder = "/lib/Bass"; REQUIRE(repo.query(f).total == 0);
    f.folder.clear();
    f.text.clear(); f.sort = LibrarySort::Duration; f.ascending = false;
    auto sorted = repo.query(f);
    REQUIRE(sorted.total == 7);
    REQUIRE(sorted.samples.front().id == kick1);
    auto secondPage = repo.query(f, 1, 1);
    REQUIRE(secondPage.total == 7);
    REQUIRE(secondPage.samples.size() == 1);
    REQUIRE(secondPage.samples.front().duration == 1.0);
}
TEST_CASE("Tags seed once, combine as all/any/exclude and survive reanalysis and reopen", "[database][tags]")
{
    TestDirectory t; const auto file = t.dir.getChildFile("tags.sqlite");
    int64_t kickId = 0, loopId = 0, customId = 0, sampleA = 0, sampleC = 0;
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        auto seeded = repo.tags();
        REQUIRE(seeded.size() == 26);
        REQUIRE(seeded.front().builtin);
        for (const auto& tag : seeded)
            if (tag.name == "Kick") kickId = tag.id;
            else if (tag.name == "Loop") loopId = tag.id;
        REQUIRE(kickId > 0); REQUIRE(loopId > 0);
        customId = repo.createTag("My Tag", 0xff123456);
        REQUIRE(customId > 0);
        REQUIRE(repo.createTag("my tag", 0xff654321) == customId);
        REQUIRE(repo.tags().size() == 27);

        sampleA = repo.upsert(sample("/a.wav", "a.wav"));
        const auto sampleB = repo.upsert(sample("/b.wav", "b.wav"));
        sampleC = repo.upsert(sample("/c.wav", "c.wav"));
        repo.setSampleTag(sampleA, kickId, true);
        repo.setSampleTag(sampleA, loopId, true);
        repo.setSampleTag(sampleB, kickId, true);
        repo.setSampleTag(sampleC, customId, true);
        REQUIRE(repo.tagsForSample(sampleA).size() == 2);

        LibraryFilter f;
        f.includeTags = { kickId, loopId };
        REQUIRE(repo.query(f).total == 1);
        f.matchAnyTag = true;
        REQUIRE(repo.query(f).total == 2);
        f.includeTags.clear(); f.matchAnyTag = false;
        f.excludeTags = { kickId };
        REQUIRE(repo.query(f).total == 1);
        f.excludeTags.clear(); f.includeTags = { customId };
        REQUIRE(repo.query(f).total == 1);
        f.text = "c"; REQUIRE(repo.query(f).total == 1);
        f.text = "zz"; REQUIRE(repo.query(f).total == 0);

        repo.setTagColour(customId, 0xff00ff00);
        for (const auto& tag : repo.tags()) if (tag.id == customId) REQUIRE(tag.colour == 0xff00ff00);
        REQUIRE(repo.tagsForSamples({ sampleA, sampleC }).size() == 2);

        auto updated = sample("/a.wav", "a.wav"); updated.duration = 9.0;
        REQUIRE(repo.upsert(updated) == sampleA);
        REQUIRE(repo.tagsForSample(sampleA).size() == 2);
        REQUIRE(repo.renameTag(customId, "Renamed"));
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.tags().size() == 27);
        bool renamed = false;
        for (const auto& tag : repo.tags()) if (tag.name == "Renamed") renamed = true;
        REQUIRE(renamed);
        REQUIRE(repo.tagsForSample(sampleA).size() == 2);
        repo.deleteTag(repo.createTag("Temporary", 0xff010203));
        REQUIRE(repo.tags().size() == 27);
        repo.deleteTag(customId);
        REQUIRE(repo.tags().size() == 26);
        REQUIRE(repo.tagsForSample(sampleC).empty());
    }
}
TEST_CASE("Schema v2 migrates to tags and a backfilled Unicode search key", "[database][migration][tags]")
{
    TestDirectory t; const auto file = t.dir.getChildFile("v2.sqlite");
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        execRaw(raw, "CREATE TABLE samples (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,"
                     "name TEXT NOT NULL, hash TEXT NOT NULL, format TEXT NOT NULL, error TEXT NOT NULL,"
                     "size INTEGER, modified INTEGER, analyzed INTEGER, duration REAL, rate REAL, bits INTEGER,"
                     "channels INTEGER, version INTEGER, peak REAL, rms REAL, centroid REAL, rolloff REAL,"
                     "flatness REAL, missing INTEGER);");
        const juce::String insertRows = juce::String("INSERT INTO samples(path,name,hash,format,error,version) VALUES('/")
            + juce::String::fromUTF8("Тест/Бас.wav") + "','" + juce::String::fromUTF8("Бас.wav") + "','h1','WAV','',1),"
            + "('/lib/kick.wav','Kick.wav','h2','WAV','',1);";
        execRaw(raw, insertRows.toRawUTF8());
        execRaw(raw, "CREATE INDEX samples_name_id ON samples(name COLLATE NOCASE, id);");
        execRaw(raw, "PRAGMA user_version=2;");
        sqlite3_close(raw);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.count() == 2);
        LibraryFilter f; f.text = juce::String::fromUTF8("тест");
        REQUIRE(repo.query(f).total == 1);
        f.text = "kick"; REQUIRE(repo.query(f).total == 1);
        REQUIRE(repo.tags().size() == 26);
        REQUIRE(rawScalar(file, "PRAGMA user_version") == 10);
        REQUIRE(rawScalar(file, "SELECT COUNT(*) FROM samples WHERE version=1") == 2);
    }
    REQUIRE(t.dir.findChildFiles(juce::File::findFiles, false, "v2.sqlite.backup-*").size() == 1);
    { Database db(file); SampleRepository repo(db); repo.createTables(); REQUIRE(repo.tags().size() == 26); }
}
TEST_CASE("User backup is a consistent copy and refuses unsafe destinations", "[database][backup]")
{
    TestDirectory t; const auto file = t.dir.getChildFile("live.sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    auto record = sample("/backup/sample.wav", "sample.wav");
    const auto id = repo.upsert(record);
    const auto tag = repo.createTag("Keep", 0xff112233);
    repo.setSampleTag(id, tag, true);

    const auto backup = t.dir.getChildFile("backups/copy.sqlite");
    repo.backupTo(backup);
    REQUIRE(backup.existsAsFile());
    {
        Database copy(backup); SampleRepository copyRepo(copy); copyRepo.createTables();
        REQUIRE(copyRepo.count() == 1);
        REQUIRE(copyRepo.findByPath("/backup/sample.wav")->fileHash == "h");
        REQUIRE(copyRepo.tagsForSample(id).size() == 1);
        REQUIRE(rawScalar(backup, "PRAGMA user_version") == 10);
    }
    REQUIRE_THROWS(repo.backupTo(file));
    REQUIRE_THROWS(repo.backupTo(backup));
}
TEST_CASE("Catalog detection rejects unrelated SQLite files", "[database][backup]")
{
    TestDirectory t;
    const auto live = t.dir.getChildFile("live.sqlite");
    { Database db(live); SampleRepository repo(db); repo.createTables(); REQUIRE(SampleRepository::isCatalog(live)); }
    const auto foreign = t.dir.getChildFile("foreign.sqlite");
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(foreign.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        execRaw(raw, "CREATE TABLE notes(text TEXT); PRAGMA user_version=5;");
        sqlite3_close(raw);
    }
    REQUIRE_FALSE(SampleRepository::isCatalog(foreign));
    REQUIRE_FALSE(SampleRepository::isCatalog(t.dir.getChildFile("absent.sqlite")));
}

TEST_CASE("Stars are batched, preserved by reanalysis and filter the catalog", "[database][starred]")
{
    TestDirectory t; Database db(t.dir.getChildFile("stars.sqlite")); SampleRepository repo(db); repo.createTables();
    const auto a = repo.upsert(sample("/a.wav", "a.wav"));
    repo.upsert(sample("/b.wav", "b.wav"));
    const auto c = repo.upsert(sample("/c.wav", "c.wav"));
    repo.setStarred({ a, c }, true);
    REQUIRE(repo.findByPath("/a.wav")->starred);
    REQUIRE_FALSE(repo.findByPath("/b.wav")->starred);
    REQUIRE(repo.findByPath("/c.wav")->starred);
    LibraryFilter filter; filter.starredOnly = true;
    REQUIRE(repo.query(filter).total == 2);
    REQUIRE(filter.hasTextOrTagFilter());

    // A rescan/upsert must not clear a user's star.
    auto updated = sample("/a.wav", "a.wav"); updated.duration = 3.0;
    REQUIRE(repo.upsert(updated) == a);
    REQUIRE(repo.findByPath("/a.wav")->starred);
    repo.setStarred({ a }, false);
    REQUIRE(repo.query(filter).total == 1);
    REQUIRE_FALSE(repo.findByPath("/a.wav")->starred);
}

TEST_CASE("Hide empty filters files with a zero peak", "[database][empty]")
{
    TestDirectory t; Database db(t.dir.getChildFile("empty.sqlite")); SampleRepository repo(db); repo.createTables();
    auto loud = sample("/lib/loud.wav", "loud.wav"); loud.peakLevel = 0.5; repo.upsert(loud);
    auto silent = sample("/lib/silent.wav", "silent.wav"); silent.peakLevel = 0.0; repo.upsert(silent);
    auto noiseFloor = sample("/lib/noise.wav", "noise.wav"); noiseFloor.peakLevel = 0.000427; repo.upsert(noiseFloor);
    LibraryFilter filter; filter.hideEmpty = true;
    REQUIRE(filter.hasTextOrTagFilter());
    const auto page = repo.query(filter);
    REQUIRE(page.total == 1);
    REQUIRE(page.samples.size() == 1);
    REQUIRE(page.samples[0].fileName == "loud.wav");
    REQUIRE(repo.queryIds(filter).size() == 1);
    filter.hideEmpty = false;
    REQUIRE(repo.query(filter).total == 3);
    REQUIRE_FALSE(filter.hasTextOrTagFilter());
}
TEST_CASE("Byte-identical duplicates group, hide and prefer the right representative", "[database][duplicates]")
{
    TestDirectory t; Database db(t.dir.getChildFile("dups.sqlite")); SampleRepository repo(db); repo.createTables();
    auto make = [](const juce::String& path, const juce::String& name, const juce::String& hash, int64_t size)
    {
        SampleRecord r; r.filePath = path; r.fileName = name; r.fileHash = hash; r.fileSize = size;
        r.format = "WAV"; r.version = basicFeaturesVersion; return r;
    };
    const auto a = repo.upsert(make("/lib/a.wav", "a.wav", "same", 1000));
    const auto b = repo.upsert(make("/lib/longer/b.wav", "b.wav", "same", 1000));
    const auto c = repo.upsert(make("/lib/c.wav", "c.wav", "different", 1000));
    const auto d = repo.upsert(make("/lib/d.wav", "d.wav", "same", 1000));
    repo.refreshDuplicates();
    auto counts = repo.duplicateCounts({ a, b, c, d });
    REQUIRE(counts[a] == 3); REQUIRE(counts[b] == 3); REQUIRE(counts[d] == 3);
    REQUIRE(counts.find(c) == counts.end());
    auto info = repo.duplicateInfo(a);
    REQUIRE(info.has_value()); REQUIRE(info->groupSize == 3); REQUIRE(info->representativeId == a);
    REQUIRE(repo.duplicateMembers(a).size() == 3);

    LibraryFilter filter; filter.hideDuplicates = true;
    REQUIRE(repo.query(filter).total == 2); // representative of the group + unique c
    filter.duplicateHash = "same";
    REQUIRE(repo.query(filter).total == 1);
    filter.hideDuplicates = false;
    REQUIRE(repo.query(filter).total == 3);

    repo.setStarred({ b }, true);
    repo.refreshDuplicates();
    REQUIRE(repo.duplicateInfo(a)->representativeId == b);
    repo.setStarred({ b }, false);
    repo.setMissing({ a });
    repo.refreshDuplicates();
    // a is missing and b/d are present; d wins on the shortest path.
    REQUIRE(repo.duplicateInfo(a)->representativeId == d);

    repo.refreshDuplicates();
    REQUIRE(repo.duplicateCounts({ a, b, d })[a] == 3);
}
TEST_CASE("ID queries and record batching preserve the catalog order", "[database][paging]")
{
    TestDirectory t; Database db(t.dir.getChildFile("ids.sqlite")); SampleRepository repo(db); repo.createTables();
    std::vector<int64_t> ids;
    for (const auto& name : { "c.wav", "a.wav", "b.wav", "d.wav" })
        ids.push_back(repo.upsert(sample(juce::String("/lib/") + name, name)));
    LibraryFilter filter;
    auto ordered = repo.queryIds(filter);
    REQUIRE(ordered.size() == 4);
    auto page = repo.query(filter, 4, 0);
    REQUIRE(page.samples.size() == ordered.size());
    for (size_t i = 0; i < ordered.size(); ++i) REQUIRE(page.samples[i].id == ordered[i]);
    std::vector<int64_t> requested { ordered[2], ordered[0], ordered[3] };
    auto records = repo.recordsByIds(requested);
    REQUIRE(records.size() == 3);
    REQUIRE(records[0].id == requested[0]);
    REQUIRE(records[1].id == requested[1]);
    REQUIRE(records[2].id == requested[2]);
    REQUIRE(repo.recordsByIds({ 999999 }).empty());
}
TEST_CASE("Relocating a root rewrites paths, keeps identity and leaves other roots alone",
          "[database][relocation]")
{
    TestDirectory t;
    auto oldRoot = t.dir.getChildFile("old-root"); oldRoot.createDirectory();
    auto newRoot = t.dir.getChildFile("new-root"); newRoot.createDirectory();
    Database db(t.dir.getChildFile("relocate.sqlite")); SampleRepository repo(db); repo.createTables();

    SampleRecord first; first.filePath = oldRoot.getChildFile("Kick/01.wav").getFullPathName();
    first.fileName = "01.wav"; first.fileHash = "h1"; first.version = basicFeaturesVersion;
    const auto firstId = repo.upsert(first);
    SampleRecord second; second.filePath = oldRoot.getChildFile("Snare/02.wav").getFullPathName();
    second.fileName = "02.wav"; second.fileHash = "h2"; second.version = basicFeaturesVersion;
    const auto secondId = repo.upsert(second);
    SampleRecord foreign; foreign.filePath = oldRoot.getFullPathName() + "\\Foreign\\x.wav";
    foreign.fileName = "x.wav"; foreign.fileHash = "h3"; const auto foreignId = repo.upsert(foreign);
    SampleRecord outside; outside.filePath = t.dir.getChildFile("elsewhere/out.wav").getFullPathName();
    outside.fileName = "out.wav"; outside.fileHash = "h4"; const auto outsideId = repo.upsert(outside);
    repo.addRoot(oldRoot.getFullPathName()); repo.addRoot(t.dir.getFullPathName());
    const auto tag = repo.createTag("Keep", 0xff112233);
    repo.setSampleTag(firstId, tag, true);
    repo.setStarred({ firstId }, true);
    repo.setMissing({ secondId });
    REQUIRE(repo.countUnderRoot(oldRoot.getFullPathName()) == 3);

    const auto result = repo.relocateRoots({{ oldRoot.getFullPathName(), newRoot.getFullPathName() }});
    REQUIRE(result.movedFiles == 3);
    REQUIRE(result.conflicts == 0);

    REQUIRE_FALSE(repo.findByPath(first.filePath).has_value());
    const auto moved = repo.findByPath(newRoot.getChildFile("Kick/01.wav").getFullPathName());
    REQUIRE(moved.has_value());
    REQUIRE(moved->id == firstId);
    REQUIRE(moved->fileHash == "h1");
    REQUIRE(moved->starred);
    REQUIRE(repo.tagsForSample(firstId).size() == 1);
    REQUIRE(repo.findByPath(newRoot.getChildFile("Snare/02.wav").getFullPathName())->id == secondId);
    REQUIRE(repo.findByPath(newRoot.getChildFile("Snare/02.wav").getFullPathName())->missing);
    REQUIRE(repo.findByPath(newRoot.getChildFile("Foreign/x.wav").getFullPathName())->id == foreignId);
    REQUIRE(repo.findByPath(outside.filePath)->id == outsideId);
    REQUIRE(repo.roots().contains(newRoot.getFullPathName()));
    REQUIRE_FALSE(repo.roots().contains(oldRoot.getFullPathName()));
    REQUIRE(repo.roots().contains(t.dir.getFullPathName()));

    LibraryFilter filter; filter.text = "new-root";
    REQUIRE(repo.query(filter).total == 3);
}

TEST_CASE("A relocation collision keeps tags and stars from both rows", "[database][relocation]")
{
    TestDirectory t;
    auto oldRoot = t.dir.getChildFile("old"); oldRoot.createDirectory();
    auto newRoot = t.dir.getChildFile("new"); newRoot.createDirectory();
    Database db(t.dir.getChildFile("collision.sqlite")); SampleRepository repo(db); repo.createTables();
    const auto movingPath = oldRoot.getChildFile("Kick/same.wav").getFullPathName();
    const auto existingPath = newRoot.getChildFile("Kick/same.wav").getFullPathName();
    SampleRecord moving; moving.filePath = movingPath; moving.fileName = "same.wav"; moving.fileHash = "m";
    const auto movingId = repo.upsert(moving);
    SampleRecord existing; existing.filePath = existingPath; existing.fileName = "same.wav"; existing.fileHash = "e";
    const auto existingId = repo.upsert(existing);
    repo.addRoot(oldRoot.getFullPathName());
    const auto movingTag = repo.createTag("Moving", 0xff111111);
    const auto existingTag = repo.createTag("Existing", 0xff222222);
    repo.setSampleTag(movingId, movingTag, true);
    repo.setSampleTag(existingId, existingTag, true);
    repo.setStarred({ movingId }, true);

    const auto result = repo.relocateRoots({{ oldRoot.getFullPathName(), newRoot.getFullPathName() }});
    REQUIRE(result.conflicts == 1);
    REQUIRE_FALSE(repo.findByPath(movingPath).has_value());
    const auto survivor = repo.findByPath(existingPath);
    REQUIRE(survivor.has_value());
    REQUIRE(survivor->id == existingId);
    REQUIRE(survivor->starred);
    auto tags = repo.tagsForSample(existingId);
    REQUIRE(tags.size() == 2);
}

TEST_CASE("A relocation with a missing target changes nothing", "[database][relocation]")
{
    TestDirectory t;
    auto oldRoot = t.dir.getChildFile("old"); oldRoot.createDirectory();
    Database db(t.dir.getChildFile("rollback.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord row; row.filePath = oldRoot.getChildFile("a.wav").getFullPathName();
    row.fileName = "a.wav"; row.fileHash = "h"; const auto id = repo.upsert(row);
    repo.addRoot(oldRoot.getFullPathName());
    const auto absent = t.dir.getChildFile("absent");
    REQUIRE_THROWS(repo.relocateRoots({{ oldRoot.getFullPathName(), absent.getFullPathName() }}));
    REQUIRE(repo.findByPath(row.filePath)->id == id);
    REQUIRE(repo.roots().contains(oldRoot.getFullPathName()));
    REQUIRE_FALSE(repo.roots().contains(absent.getFullPathName()));
}

TEST_CASE("A Windows-style root relocates on a POSIX host", "[database][relocation]")
{
    TestDirectory t;
    auto newRoot = t.dir.getChildFile("foreign-target"); newRoot.createDirectory();
    Database db(t.dir.getChildFile("foreign.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord row; row.filePath = "C:\\Samples\\Drums\\Kick\\01.wav"; row.fileName = "01.wav"; row.fileHash = "h";
    const auto id = repo.upsert(row);
    repo.addRoot("C:\\Samples\\Drums");
    REQUIRE(repo.countUnderRoot("C:\\Samples\\Drums") == 1);

    const auto result = repo.relocateRoots({{ "C:\\Samples\\Drums", newRoot.getFullPathName() }});
    REQUIRE(result.movedFiles == 1);
    const auto moved = repo.findByPath(newRoot.getChildFile("Kick/01.wav").getFullPathName());
    REQUIRE(moved.has_value());
    REQUIRE(moved->id == id);
    REQUIRE(repo.roots().contains(newRoot.getFullPathName()));
    REQUIRE_FALSE(repo.roots().contains("C:\\Samples\\Drums"));
}
TEST_CASE("Schema 8 catalogs gain the new instrument tags without touching user tags", "[database][migration][tags]")
{
    TestDirectory t; const auto file = t.dir.getChildFile("tags-v8.sqlite");
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        repo.createTag("My Tag", 0xff123456);
    }
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw,
            "DELETE FROM tags WHERE name IN ('Crash','Ride','Tom','Rim','Fill','Riser',"
            "'Downfilter','Upfilter','Snap','Shaker','Tamb'); PRAGMA user_version=8;",
            nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        const auto tags = repo.tags();
        REQUIRE(tags.size() == 27); // 26 built-ins plus My Tag
        bool hasCrash = false, hasTom = false, hasUser = false;
        for (const auto& tag : tags)
        {
            if (tag.name == "Crash") hasCrash = true;
            if (tag.name == "Tom") hasTom = true;
            if (tag.name == "My Tag") hasUser = true;
        }
        REQUIRE(hasCrash); REQUIRE(hasTom); REQUIRE(hasUser);
        REQUIRE(rawScalar(file, "PRAGMA user_version") == 10);
    }
    REQUIRE(t.dir.findChildFiles(juce::File::findFiles, false, "tags-v8.sqlite.backup-*").size() == 1);
}

TEST_CASE("Compatibility round-trips and Hide empty hides incompatible assets", "[database][asset-compat]")
{
    TestDirectory t; Database db(t.dir.getChildFile("compat.sqlite")); SampleRepository repo(db); repo.createTables();
    auto make = [](const juce::String& path, AssetCompatibility compatibility, double peak, int version)
    {
        SampleRecord r; r.filePath = path; r.fileName = juce::File(path).getFileName();
        r.fileHash = path; r.format = "WAV"; r.version = version; r.peakLevel = peak;
        r.compatibility = compatibility; return r;
    };
    repo.upsert(make("/lib/supported.wav", AssetCompatibility::supported, 0.5, basicFeaturesVersion));
    repo.upsert(make("/lib/silent.wav", AssetCompatibility::supported, 0.0, basicFeaturesVersion));
    repo.upsert(make("/lib/unknown.wav", AssetCompatibility::unknown, 0.5, basicFeaturesVersion));
    repo.upsert(make("/lib/invalid.wav", AssetCompatibility::invalid, 0.5, basicFeaturesVersion));
    repo.upsert(make("/lib/unsupported.rx2", AssetCompatibility::unsupported, 0.0, metadataOnlyVersion));
    REQUIRE(repo.findByPath("/lib/invalid.wav")->compatibility == AssetCompatibility::invalid);
    REQUIRE(repo.findByPath("/lib/unsupported.rx2")->compatibility == AssetCompatibility::unsupported);

    LibraryFilter filter; filter.hideEmpty = true;
    const auto page = repo.query(filter);
    REQUIRE(page.total == 2); // supported + unknown; silent, invalid and unsupported are hidden
    REQUIRE(repo.queryIds(filter).size() == 2);
    filter.hideEmpty = false;
    REQUIRE(repo.query(filter).total == 5);
}
