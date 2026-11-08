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
