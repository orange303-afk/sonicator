#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <algorithm>
#include <sqlite3.h>
using namespace sonicator;
namespace {
struct TagFixture
{
    TestDirectory t;
    std::unique_ptr<Database> db;
    std::unique_ptr<SampleRepository> repo;
    int64_t sampleId = 0;
    explicit TagFixture(const juce::String& name = "recent.sqlite")
    {
        db = std::make_unique<Database>(t.dir.getChildFile(name));
        repo = std::make_unique<SampleRepository>(*db);
        repo->createTables();
        SampleRecord r; r.filePath = t.dir.getChildFile("audio/kick.wav").getFullPathName();
        r.fileName = "kick.wav"; r.fileHash = "hash"; sampleId = repo->upsert(r);
    }
};
}
TEST_CASE("Recent tags keep five unique entries newest first", "[tags][recent]")
{
    TagFixture f;
    std::vector<int64_t> ids;
    for (int i = 1; i <= 6; ++i)
        ids.push_back(f.repo->createTag("Tag " + juce::String(i), 0xff000000 + (uint32_t) i));
    for (auto id : ids)
    {
        REQUIRE(f.repo->assignTag({ f.sampleId }, id) == 1);
        f.repo->recordRecentTag(id);
    }
    auto recent = f.repo->recentTags();
    REQUIRE(recent.size() == 5);
    REQUIRE(recent[0].id == ids[5]);
    REQUIRE(recent[4].id == ids[1]);
    // Re-assigning an existing tag does not create a duplicate and moves it to the front.
    REQUIRE(f.repo->assignTag({ f.sampleId }, ids[1]) == 0);
    f.repo->recordRecentTag(ids[1]);
    recent = f.repo->recentTags();
    REQUIRE(recent.size() == 5);
    REQUIRE(recent[0].id == ids[1]);
    REQUIRE(recent[1].id == ids[5]);
}
TEST_CASE("Recent tags follow rename and deletion and survive a reopen", "[tags][recent]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-recent-reopen", ".sqlite");
    int64_t first = 0, second = 0;
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        first = repo.createTag("First", 0xff111111);
        second = repo.createTag("Second", 0xff222222);
        repo.recordRecentTag(first);
        repo.recordRecentTag(second);
        REQUIRE(repo.renameTag(first, "Renamed"));
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        auto recent = repo.recentTags();
        REQUIRE(recent.size() == 2);
        REQUIRE(recent[0].id == second);
        REQUIRE(recent[1].name == "Renamed");
        repo.deleteTag(first);
        recent = repo.recentTags();
        REQUIRE(recent.size() == 1);
        REQUIRE(recent[0].id == second);
    }
    REQUIRE(file.deleteFile());
}
TEST_CASE("Recent tags are scoped to one catalog", "[tags][recent][catalog]")
{
    TagFixture a("a.sqlite"), b("b.sqlite");
    const auto tag = a.repo->createTag("Kick", 0xff00ff00);
    a.repo->recordRecentTag(tag);
    REQUIRE(a.repo->recentTags().size() == 1);
    REQUIRE(b.repo->recentTags().empty());
}
TEST_CASE("Batch tag assignment reports new links only and never removes", "[tags][assignment]")
{
    TagFixture f;
    const auto tag = f.repo->createTag("Kick", 0xff00ff00);
    std::vector<int64_t> ids { f.sampleId };
    for (int i = 0; i < 2; ++i)
    {
        SampleRecord r; r.filePath = f.t.dir.getChildFile("audio/" + juce::String(i) + ".wav").getFullPathName();
        r.fileName = juce::String(i) + ".wav"; r.fileHash = "h" + juce::String(i);
        ids.push_back(f.repo->upsert(r));
    }
    REQUIRE(f.repo->assignTag(ids, tag) == 3);
    REQUIRE(f.repo->assignTag(ids, tag) == 0);
    REQUIRE(f.repo->tagsForSample(ids[0]).size() == 1);
    REQUIRE(f.repo->tagsForSample(ids[1]).size() == 1);
    REQUIRE(f.repo->tagsForSample(ids[2]).size() == 1);
}
TEST_CASE("Folder id scope is recursive and separator-aware", "[tags][folders]")
{
    TagFixture f;
    const auto root = f.t.dir.getChildFile("lib").getFullPathName();
    auto add = [&](const juce::String& relative)
    {
        SampleRecord r; r.filePath = root + "/" + relative;
        r.fileName = juce::File(r.filePath).getFileName(); r.fileHash = relative;
        return f.repo->upsert(r);
    };
    const auto direct = add("Drums/k.wav");
    const auto deep = add("Drums/deep/s.wav");
    add("DrumsExtra/no.wav");
    add("Other/x.wav");
    auto ids = f.repo->idsInFolder(root + "/Drums");
    REQUIRE(ids.size() == 2);
    REQUIRE(std::find(ids.begin(), ids.end(), direct) != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), deep) != ids.end());
    REQUIRE(f.repo->countWithTagInFolder(root + "/Drums", f.repo->createTag("X", 0xff000000)) == 0);
}
TEST_CASE("Schema v7 catalogs upgrade to v8 with a backup", "[tags][recent][migration]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("legacy7.sqlite");
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/lib/kick.wav"; r.fileName = "kick.wav"; r.fileHash = "h";
        repo.upsert(r);
    }
    {
        // Simulate a catalog written before recent-tag history existed.
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw, "DROP TABLE recent_tags; PRAGMA user_version=7;", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        int64_t version = -1;
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, "PRAGMA user_version", -1, &s, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(s) == SQLITE_ROW);
        version = sqlite3_column_int(s, 0);
        sqlite3_finalize(s); sqlite3_close(raw);
        REQUIRE(version == 13);
        const auto tag = repo.createTag("Kick", 0xff00ff00);
        repo.recordRecentTag(tag);
        REQUIRE(repo.recentTags().size() == 1);
    }
    const auto backups = t.dir.findChildFiles(juce::File::findFiles, false, "legacy7.sqlite.backup-*");
    REQUIRE(backups.size() == 1);
}
