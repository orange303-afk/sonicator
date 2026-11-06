#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/database/SampleRepository.h"
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
        r.version = analysisVersion;
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
        REQUIRE(r->version == analysisVersion);
        REQUIRE(repo.page(1, 0).size() == 1);
        REQUIRE(repo.page(1, 2).empty());
        REQUIRE_FALSE(repo.findByPath("absent"));
    }
    REQUIRE(file.deleteFile());
}
TEST_CASE("Database open failure is reported", "[database]")
{
    REQUIRE_THROWS(Database(juce::File::getSpecialLocation(juce::File::tempDirectory)));
}
