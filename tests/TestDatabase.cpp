#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <sqlite3.h>
using namespace sonicator;
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
                     "('/b.wav','b.wav','h2','WAV','',1);");
        execRaw(raw, "PRAGMA user_version=1;");
        sqlite3_close(raw);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.count() == 2);
    }
    REQUIRE(t.dir.findChildFiles(juce::File::findFiles, false, "legacy.sqlite.backup-*").size() == 1);
    REQUIRE(rawScalar(file, "PRAGMA user_version") == 2);
    REQUIRE(rawScalar(file, "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='samples_name_id'") == 1);
    { Database db(file); SampleRepository repo(db); repo.createTables(); REQUIRE(repo.count() == 2); }
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
