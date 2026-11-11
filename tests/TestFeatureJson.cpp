#include "core/dsp/FeatureJson.h"
#include <catch2/catch_test_macros.hpp>

using namespace sonicator;

TEST_CASE("featureJsonLine is one deterministic JSON line", "[dsp][features][json]")
{
    FeatureVectorV2 vector;
    vector.values.assign(static_cast<size_t>(FeatureExtractorV2::dimensions()), 0.25f);

    const auto line = featureJsonLine(vector, "/tmp/a.wav", 44100.0, 1.5, 22050.0);
    REQUIRE(line.startsWith("{"));
    REQUIRE_FALSE(line.contains("\n"));
    REQUIRE(line.contains("\"featureSchema\":2"));
    REQUIRE(line.contains("\"dimensions\":84"));
    REQUIRE(line.contains("\"durationSec\":1.5"));
    REQUIRE(line.contains("\"bandwidthHz\":22050"));
    REQUIRE(line == featureJsonLine(vector, "/tmp/a.wav", 44100.0, 1.5, 22050.0));
}

TEST_CASE("featureJsonLine escapes a path with quotes and backslashes", "[dsp][features][json]")
{
    FeatureVectorV2 vector;
    vector.values.assign(static_cast<size_t>(FeatureExtractorV2::dimensions()), 0.0f);
    const auto line = featureJsonLine(vector, "/tmp/a \"odd\" \\ path.wav", 16000.0, 0.5, 8000.0);
    const auto parsed = juce::JSON::parse(line);
    REQUIRE(parsed.isObject());
    REQUIRE(parsed.getProperty("path", {}).toString() == "/tmp/a \"odd\" \\ path.wav");
    REQUIRE(static_cast<int>(parsed.getProperty("dimensions", 0)) == 84);
    REQUIRE(static_cast<int>(parsed.getProperty("features", juce::var()).size()) == 84);
}

TEST_CASE("featureJsonLine reports a failed extraction with an empty vector", "[dsp][features][json]")
{
    FeatureVectorV2 failed;
    failed.error = "non-finite sample";
    const auto line = featureJsonLine(failed, "/tmp/bad.wav", 44100.0, 1.0, 22050.0);
    const auto parsed = juce::JSON::parse(line);
    REQUIRE(static_cast<int>(parsed.getProperty("features", juce::var()).size()) == 0);
}
