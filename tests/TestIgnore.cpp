#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "core/indexing/IndexingService.h"
#include "core/dsp/BasicAnalyzer.h"
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

TEST_CASE("Removing a folder deletes only its rows and keeps it out of parent rescans", "[database][remove]")
{
    TestDirectory t;
    const auto root = t.dir.getChildFile("Root");
    const auto keep = t.wave("Root/Keep/keep.wav");
    const auto gone = t.wave("Root/Gone/kick.wav");
    const auto similar = t.wave("Root/Gone2/snare.wav");
    Database db(t.dir.getChildFile("remove-folder.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(root.getFullPathName());
    auto keepId = repo.upsert(BasicAnalyzer().analyze(keep));
    const auto goneId = repo.upsert(BasicAnalyzer().analyze(gone));
    repo.upsert(BasicAnalyzer().analyze(similar));
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    repo.setSampleTag(goneId, tag, true);

    const auto removed = repo.removeFolder(t.dir.getChildFile("Root/Gone").getFullPathName());
    REQUIRE(removed.rowsRemoved == 1);
    REQUIRE_FALSE(removed.wasRoot);
    REQUIRE_FALSE(repo.findByPath(gone.getFullPathName()).has_value());
    REQUIRE(repo.tagsForSample(goneId).empty());
    REQUIRE(repo.isIgnored(t.dir.getChildFile("Root/Gone").getFullPathName()));
    REQUIRE(repo.findByPath(keep.getFullPathName()).has_value());
    REQUIRE(repo.findByPath(similar.getFullPathName()).has_value()); // Gone2 is not Gone
    REQUIRE(repo.roots().contains(root.getFullPathName()));

    // A later parent scan must not resurrect the removed subtree.
    IndexingService indexer(repo);
    REQUIRE(indexer.start({root.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(20000));
    REQUIRE_FALSE(repo.findByPath(gone.getFullPathName()).has_value());
    REQUIRE(repo.findByPath(keep.getFullPathName()).has_value());

    const auto rootRemoved = repo.removeFolder(root.getFullPathName());
    REQUIRE(rootRemoved.wasRoot);
    REQUIRE(rootRemoved.rowsRemoved >= 2);
    REQUIRE(repo.roots().isEmpty());
    REQUIRE(repo.isIgnored(root.getFullPathName())); // explicit re-add clears this
    REQUIRE_FALSE(repo.findByPath(keep.getFullPathName()).has_value());
    REQUIRE(keep.existsAsFile()); // files on disk are never touched
    REQUIRE(keepId > 0);
}
TEST_CASE("Removing a nested registered root keeps it out of the parent rescan", "[database][remove]")
{
    TestDirectory t;
    const auto root = t.dir.getChildFile("Root");
    const auto nested = t.dir.getChildFile("Root/Gone");
    const auto feed = t.wave("Root/Gone/kick.wav");
    const auto keep = t.wave("Root/Keep/keep.wav");
    Database db(t.dir.getChildFile("nested-root.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(root.getFullPathName());
    repo.addRoot(nested.getFullPathName());
    REQUIRE(repo.upsert(BasicAnalyzer().analyze(feed)) > 0);
    REQUIRE(repo.upsert(BasicAnalyzer().analyze(keep)) > 0);

    const auto removed = repo.removeFolder(nested.getFullPathName());
    REQUIRE(removed.wasRoot); // it was registered on its own
    REQUIRE(repo.isIgnored(nested.getFullPathName())); // and is still inside the parent root
    REQUIRE(repo.roots().contains(root.getFullPathName()));
    REQUIRE_FALSE(repo.roots().contains(nested.getFullPathName()));

    IndexingService indexer(repo);
    REQUIRE(indexer.start({root.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(20000));
    REQUIRE_FALSE(repo.findByPath(feed.getFullPathName()).has_value());
    REQUIRE(repo.findByPath(keep.getFullPathName()).has_value());
}
TEST_CASE("Explicitly adding a folder clears ignores of itself and its ancestors", "[database][remove]")
{
    TestDirectory t;
    const auto parent = t.dir.getChildFile("Removed");
    const auto child = t.wave("Removed/Sub/kick.wav");
    Database db(t.dir.getChildFile("covering.sqlite")); SampleRepository repo(db); repo.createTables();
    const auto id = repo.upsert(BasicAnalyzer().analyze(child));
    repo.ignorePath(parent.getFullPathName(), true);
    REQUIRE(repo.isIgnored(child.getFullPathName()));
    REQUIRE(repo.visibleIds({ id }).empty());

    REQUIRE(repo.unignoreCoveringFolders(t.dir.getChildFile("Removed/Sub").getFullPathName()) == 1);
    REQUIRE_FALSE(repo.isIgnored(parent.getFullPathName()));
    REQUIRE_FALSE(repo.isIgnored(child.getFullPathName()));
    REQUIRE(repo.visibleIds({ id }) == std::vector<int64_t>{ id });
    REQUIRE(repo.unignoreCoveringFolders(t.dir.getChildFile("Removed/Sub").getFullPathName()) == 0);
}
TEST_CASE("Removing a folder drops unsupported archive records only", "[database][remove][reason]")
{
    TestDirectory t; Database db(t.dir.getChildFile("formats.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SampleRecord cmb, rfl, patch, wav;
    cmb.filePath = "/Packs/Test.cmb"; cmb.fileName = "Test.cmb"; cmb.format = "CMB";
    rfl.filePath = "/Refills/Bank.rfl"; rfl.fileName = "Bank.rfl"; rfl.format = "RFL";
    patch.filePath = "/Packs/Test.repatch"; patch.fileName = "Test.repatch"; patch.format = "REPATCH";
    wav.filePath = "/Packs/loop.wav"; wav.fileName = "loop.wav"; wav.format = "WAV";
    for (const auto& record : { cmb, rfl, patch, wav }) repo.upsert(record);
    REQUIRE(repo.removeSamplesWithFormats({ "CMB", "RFL" }) == 2);
    REQUIRE_FALSE(repo.findByPath(cmb.filePath).has_value());
    REQUIRE_FALSE(repo.findByPath(rfl.filePath).has_value());
    REQUIRE(repo.findByPath(patch.filePath).has_value());
    REQUIRE(repo.findByPath(wav.filePath).has_value());
    REQUIRE(repo.removeSamplesWithFormats({ "CMB", "RFL" }) == 0);
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
