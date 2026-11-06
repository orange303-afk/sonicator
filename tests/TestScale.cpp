#include <catch2/catch_test_macros.hpp>
#include "core/indexing/IndexingService.h"
#include "TestHelpers.h"
#include <iostream>
using namespace sonicator;
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
