#include <catch2/catch_test_macros.hpp>
#include "core/dsp/BasicAnalyzer.h"
#include "core/database/SampleRepository.h"
#include <iostream>
using namespace sonicator;

// Opt-in only. All writes go to the explicitly supplied output directory, never the library.
TEST_CASE("Read-only validation of an external sample-library selection", "[.external]")
{
    auto manifest = juce::SystemStats::getEnvironmentVariable("SONICATOR_EXTERNAL_MANIFEST",{});
    auto outputPath = juce::SystemStats::getEnvironmentVariable("SONICATOR_EXTERNAL_OUTPUT",{});
    REQUIRE(manifest.isNotEmpty()); REQUIRE(outputPath.isNotEmpty());
    auto selection = juce::JSON::parse(juce::File(manifest));
    REQUIRE(selection.isArray()); REQUIRE(selection.size() > 0);
    juce::File output(outputPath); REQUIRE(output.createDirectory().wasOk());
    Database db(output.getChildFile("external-validation.sqlite")); SampleRepository repo(db); repo.createTables();
    juce::Array<juce::var> results;
    int succeeded = 0, failed = 0, cancelled = 0, excludedMetadata = 0;
    auto started = juce::Time::getMillisecondCounterHiRes();
    for (auto& value : *selection.getArray())
    {
        auto path = value["path"].toString(); juce::File file(path);
        REQUIRE(file.existsAsFile());
        if (! FileScanner::isSupportedFormat(file)) { ++excludedMetadata; continue; }
        auto beforeSize = file.getSize(); auto beforeTime = file.getLastModificationTime();
        auto hash = FileScanner::computeFileHash(file);
        auto sampleStart = juce::Time::getMillisecondCounterHiRes();
        auto result = BasicAnalyzer().analyze(file,[sampleStart]
        { return juce::Time::getMillisecondCounterHiRes() - sampleStart > 10000; });
        double elapsed = (juce::Time::getMillisecondCounterHiRes()-sampleStart)/1000;
        REQUIRE(file.getSize() == beforeSize); REQUIRE(file.getLastModificationTime() == beforeTime);
        REQUIRE(FileScanner::computeFileHash(file) == hash);
        if (result.error == "Cancelled") ++cancelled;
        else if (result.error.isNotEmpty()) ++failed;
        else
        {
            ++succeeded;
            REQUIRE(std::isfinite(result.peakLevel)); REQUIRE(std::isfinite(result.rmsLevel));
            REQUIRE(std::isfinite(result.spectralCentroid)); REQUIRE(std::isfinite(result.spectralFlatness));
        }
        result.fileHash = hash; repo.upsert(result);
        auto* item = new juce::DynamicObject();
        item->setProperty("path",path); item->setProperty("format",result.format);
        item->setProperty("channels",result.numChannels); item->setProperty("sample_rate",result.sampleRate);
        item->setProperty("duration",result.duration); item->setProperty("analysis_seconds",elapsed);
        item->setProperty("error",result.error); results.add(juce::var(item));
    }
    auto* report = new juce::DynamicObject();
    report->setProperty("succeeded",succeeded); report->setProperty("failed",failed);
    report->setProperty("time_limited",cancelled);
    report->setProperty("excluded_metadata",excludedMetadata);
    report->setProperty("total_seconds",(juce::Time::getMillisecondCounterHiRes()-started)/1000);
    report->setProperty("files",juce::var(results));
    auto json = juce::JSON::toString(juce::var(report));
    REQUIRE(output.getChildFile("external-validation.json").replaceWithText(json));
    std::cout << "External library: " << succeeded << " analyzed, " << failed << " errors, "
              << cancelled << " time-limited, " << excludedMetadata << " metadata forks excluded\n";
    REQUIRE(succeeded > 0);
}
TEST_CASE("Production scanner enumerates a large external library read-only", "[.external-scan]")
{
    auto root = juce::SystemStats::getEnvironmentVariable("SONICATOR_EXTERNAL_ROOT",{});
    REQUIRE(root.isNotEmpty());
    auto start = juce::Time::getMillisecondCounterHiRes();
    auto files = FileScanner::scanDirectory(juce::File(root));
    REQUIRE(files.size() >= 10000);
    std::cout << "Production scanner: " << files.size() << " supported files in "
              << (juce::Time::getMillisecondCounterHiRes()-start)/1000 << " s\n";
}
