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
