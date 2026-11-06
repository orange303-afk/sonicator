#include <catch2/catch_test_macros.hpp>
#include "core/indexing/IndexingService.h"
#include "TestHelpers.h"
#include <iostream>
#include <sqlite3.h>
using namespace sonicator;
TEST_CASE("Three hundred thousand rows finalize quickly with correct missing flags", "[.scale]")
{
    TestDirectory t;
    auto audio = t.dir.getChildFile("audio"); audio.createDirectory();
    t.wave("audio/keep.wav", 440);
    auto dbPath = t.dir.getChildFile("catalog.sqlite");
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    {
        sqlite3* raw = nullptr; REQUIRE(sqlite3_open(dbPath.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, "INSERT INTO samples(path,name,hash,format,error,size,modified,version,missing)"
            " VALUES(?,?,'h','WAV','',0,0,1,0)", -1, &s, nullptr) == SQLITE_OK);
        for (int i = 0; i < 300000; ++i)
        {
            const auto name = "ghost_" + juce::String(i) + ".wav";
            const auto path = audio.getChildFile(name).getFullPathName();
            sqlite3_bind_text(s, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
            REQUIRE(sqlite3_step(s) == SQLITE_DONE);
            sqlite3_reset(s);
        }
        sqlite3_finalize(s);
        REQUIRE(sqlite3_exec(raw, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    {
        sqlite3* raw = nullptr; REQUIRE(sqlite3_open(dbPath.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, "EXPLAIN QUERY PLAN SELECT id,path,missing FROM samples WHERE id>0 ORDER BY id LIMIT 5000", -1, &s, nullptr) == SQLITE_OK);
        bool tempSort = false;
        while (sqlite3_step(s) == SQLITE_ROW)
            if (juce::String((const char*) sqlite3_column_text(s, 3)).containsIgnoreCase("TEMP B-TREE")) tempSort = true;
        sqlite3_finalize(s);
        REQUIRE(sqlite3_prepare_v2(raw, "EXPLAIN QUERY PLAN SELECT id FROM samples ORDER BY name COLLATE NOCASE,id LIMIT 10000", -1, &s, nullptr) == SQLITE_OK);
        bool usesSortIndex = false;
        while (sqlite3_step(s) == SQLITE_ROW)
            if (juce::String((const char*) sqlite3_column_text(s, 3)).containsIgnoreCase("samples_name_id")) usesSortIndex = true;
        sqlite3_finalize(s); sqlite3_close(raw);
        REQUIRE_FALSE(tempSort);
        REQUIRE(usesSortIndex);
    }
    IndexingService index(repo);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    REQUIRE(index.start({audio.getFullPathName()})); REQUIRE(index.waitUntilFinished(120000));
    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    REQUIRE(repo.count() == 300001);
    REQUIRE_FALSE(repo.findByPath(audio.getChildFile("keep.wav").getFullPathName())->missing);
    REQUIRE(index.progress().analyzed >= 1);
    REQUIRE(seconds < 60.0);
    std::cout << "300k reconciliation: " << seconds << " s\n";
}
TEST_CASE("Index and reopen ten thousand real WAV files", "[.benchmark]")
{
    TestDirectory t;
    auto original = t.wave("source.wav", 440, 1, 2048);
    auto folder = t.dir.getChildFile("library"); folder.createDirectory();
    for (int i = 0; i < 10000; ++i)
        REQUIRE(original.copyFileTo(folder.getChildFile("sample_" + juce::String(i) + ".wav")));
    const auto path = t.dir.getChildFile("library.sqlite");
    const auto before = juce::Time::getMillisecondCounterHiRes();
    {
        Database db(path); SampleRepository repo(db); repo.createTables(); IndexingService index(repo);
        REQUIRE(index.start({folder.getFullPathName()})); REQUIRE(index.waitUntilFinished(180000));
        auto p = index.progress(); REQUIRE(p.analyzed == 10000); REQUIRE(p.failed == 0);
        REQUIRE(repo.count() == 10000);
        std::cout << "Initial 10000-file index: " << (juce::Time::getMillisecondCounterHiRes()-before)/1000 << " s\n";
    }
    {
        Database db(path); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.count() == 10000); REQUIRE(repo.page(10000).size() == 10000);
        IndexingService index(repo);
        auto start = juce::Time::getMillisecondCounterHiRes();
        REQUIRE(index.start({folder.getFullPathName()})); REQUIRE(index.waitUntilFinished(60000));
        REQUIRE(index.progress().skipped == 10000); REQUIRE(index.progress().analyzed == 0);
        std::cout << "Unchanged rescan: " << (juce::Time::getMillisecondCounterHiRes()-start)/1000 << " s\n";
    }
}
TEST_CASE("A sparse WAV larger than one gigabyte is decoded incrementally and cancellable", "[.large-file]")
{
    TestDirectory t; auto file = t.dir.getChildFile("large.wav");
    constexpr int bytes = 1200000000;
    {
        auto stream = file.createOutputStream(); REQUIRE(stream != nullptr);
        stream->write("RIFF",4); stream->writeInt(bytes+36); stream->write("WAVEfmt ",8);
        stream->writeInt(16); stream->writeShort(1); stream->writeShort(1);
        stream->writeInt(44100); stream->writeInt(88200); stream->writeShort(2); stream->writeShort(16);
        stream->write("data",4); stream->writeInt(bytes);
        REQUIRE(stream->setPosition((int64_t) bytes+43)); stream->writeByte(0);
    }
    REQUIRE(file.getSize() == (int64_t) bytes+44);
    int chunks = 0;
    auto result = BasicAnalyzer().analyze(file,[&] { return ++chunks > 8; });
    REQUIRE(result.error == "Cancelled"); REQUIRE(chunks == 9);
    REQUIRE(result.duration > 13000);
}
