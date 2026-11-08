#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <sqlite3.h>
using namespace sonicator;

TEST_CASE("Ignore entries persist and match folder descendants", "[ignore][database]")
{
    TestDirectory t;
    auto dbFile = t.dir.getChildFile("ignore.sqlite");
    const auto folder = t.dir.getChildFile("Drums").getFullPathName();
    const auto inside = folder + "/kick.wav";
    const auto nearby = t.dir.getChildFile("Drums2/snare.wav").getFullPathName();
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        repo.ignorePath(folder, true);
        repo.ignorePath(inside, false);
        REQUIRE(repo.isIgnored(folder));
        REQUIRE(repo.isIgnored(inside));
        REQUIRE(repo.isIgnored(folder + "/deep/more.wav"));
        REQUIRE_FALSE(repo.isIgnored(nearby));
        REQUIRE(repo.hasExactIgnore(folder));
        REQUIRE(repo.hasExactIgnore(inside));
        REQUIRE_FALSE(repo.hasExactIgnore(folder + "/deep/more.wav"));
        REQUIRE(repo.ignoredPaths().size() == 2);
    }
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.isIgnored(inside));
        repo.unignorePath(folder);
        REQUIRE_FALSE(repo.isIgnored(folder + "/deep/more.wav"));
        REQUIRE(repo.isIgnored(inside)); // the exact entry survives
    }
}

TEST_CASE("Dropping ignored_paths at schema 10 recreates it and keeps rows", "[ignore][database][migration]")
{
    TestDirectory t;
    auto dbFile = t.dir.getChildFile("migrate.sqlite");
    int64_t id = 0;
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/lib/a.wav"; r.fileName = "a.wav";
        id = repo.upsert(r);
    }
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw, "DROP TABLE ignored_paths; PRAGMA user_version=10;", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.findByPath("/lib/a.wav")->id == id);
        REQUIRE(repo.count() == 1);
        repo.ignorePath("/lib/a.wav", false);
        REQUIRE(repo.isIgnored("/lib/a.wav"));
    }
}

TEST_CASE("Ignored rows leave queries, counts, folder scopes and duplicate groups", "[ignore][database][query]")
{
    TestDirectory t; Database db(t.dir.getChildFile("query.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto root = (t.dir.getFullPathName() + "/lib");
    auto add = [&](const juce::String& path, const juce::String& hash)
    {
        SampleRecord r; r.filePath = path; r.fileName = juce::File(path).getFileName();
        r.fileHash = hash; r.fileSize = 10; r.version = basicFeaturesVersion;
        return repo.upsert(r);
    };
    const auto keep = add(root + "/keep.wav", "same");
    const auto ignoredFile = add(root + "/hidden/hide.wav", "same");
    const auto folderFile = add(root + "/skip/deep/gone.wav", "");
    juce::ignoreUnused(folderFile);

    repo.ignorePath(root + "/hidden/hide.wav", false);
    repo.ignorePath(root + "/skip", true);
    repo.refreshDuplicates();

    LibraryFilter f; f.folder = root;
    REQUIRE(repo.query(f).total == 1);
    REQUIRE(repo.queryIds(f) == std::vector<int64_t>{ keep });
    REQUIRE(repo.count() == 1);
    REQUIRE(repo.count(true) == 3);
    REQUIRE(repo.countInFolder(root) == 1);
    REQUIRE(repo.pageInFolder(root).size() == 1);
    REQUIRE(repo.idsInFolder(root) == std::vector<int64_t>{ keep });

    f.showIgnored = true;
    REQUIRE(repo.query(f).total == 3);
    REQUIRE(repo.queryIds(f).size() == 3);

    REQUIRE_FALSE(repo.duplicateInfo(keep).has_value());
    REQUIRE_FALSE(repo.duplicateInfo(ignoredFile).has_value());
    REQUIRE(repo.samplesNeedingSmartAnalysis(0, 100) == std::vector<int64_t>{ keep });
}

TEST_CASE("Large ignore lists keep queries working", "[ignore][database][large]")
{
    TestDirectory t; Database db(t.dir.getChildFile("large.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SampleRecord keep; keep.filePath = "/lib/keep.wav"; keep.fileName = "keep.wav"; repo.upsert(keep);
    for (int i = 0; i < 998; ++i)
        repo.ignorePath("/lib/files/f" + juce::String(i) + ".wav", false);
    for (int i = 0; i < 120; ++i)
        repo.ignorePath("/lib/folders/f" + juce::String(i), true);
    LibraryFilter f; f.folder = "/lib";
    REQUIRE(repo.query(f).total == 1);
    REQUIRE(repo.count() == 1);
    REQUIRE(repo.queryIds(f).size() == 1);
}

TEST_CASE("visibleIds keeps only rows that are not ignored", "[ignore][database][bulk]")
{
    TestDirectory t; Database db(t.dir.getChildFile("visible.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SampleRecord a, b; a.filePath = "/lib/a.wav"; a.fileName = "a.wav";
    b.filePath = "/lib/skip/b.wav"; b.fileName = "b.wav";
    const auto keep = repo.upsert(a);
    const auto hidden = repo.upsert(b);
    repo.ignorePath("/lib/skip", true);
    REQUIRE(repo.visibleIds({ keep, hidden }) == std::vector<int64_t>{ keep });
    REQUIRE(repo.visibleIds({ hidden }).empty());
}

TEST_CASE("Smart verification skips ignored completed results", "[ignore][database][smart]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("smart.sqlite");
    int64_t keep = 0, hidden = 0;
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        SampleRecord a, b; a.filePath = "/lib/a.wav"; a.fileName = "a.wav";
        b.filePath = "/lib/skip/b.wav"; b.fileName = "b.wav";
        keep = repo.upsert(a); hidden = repo.upsert(b);
    }
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        const auto sql = "INSERT INTO smart_results(sample_id,pipeline_state) VALUES("
            + juce::String(keep) + ",'complete'),(" + juce::String(hidden) + ",'complete');";
        REQUIRE(sqlite3_exec(raw, sql.toRawUTF8(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        repo.ignorePath("/lib/skip", true);
        REQUIRE(repo.samplesWithSmartResultAfterId(0, 100) == std::vector<int64_t>{ keep });
        const auto needing = repo.samplesNeedingSmartAnalysis(0, 100);
        REQUIRE(std::find(needing.begin(), needing.end(), hidden) == needing.end());
    }
}

TEST_CASE("removeByPaths drops rows, tag links and exact ignore entries", "[database][remove]")
{
    TestDirectory t; Database db(t.dir.getChildFile("remove.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/gone.wav"; r.fileName = "gone.wav";
    const auto id = repo.upsert(r);
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    repo.setSampleTag(id, tag, true);
    repo.ignorePath("/lib/gone.wav", false);
    repo.removeByPaths({ "/lib/gone.wav" });
    REQUIRE_FALSE(repo.findByPath("/lib/gone.wav").has_value());
    REQUIRE_FALSE(repo.hasExactIgnore("/lib/gone.wav"));
    REQUIRE(repo.tagsForSample(id).empty());
}
