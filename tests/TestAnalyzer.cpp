#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/BasicAnalyzer.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Streaming analyzer measures a sine, including inverted stereo", "[analyzer]")
{
    TestDirectory t;
    for (int channels : {1, 2})
    {
        auto r = BasicAnalyzer().analyze(t.wave("sine.wav", 440, channels, 44100, true));
        REQUIRE(r.error.isEmpty());
        REQUIRE(r.version == analysisVersion);
        REQUIRE(r.duration == Catch::Approx(1));
        REQUIRE(r.sampleRate == 44100);
        REQUIRE(r.numChannels == channels);
        REQUIRE(r.bitsPerSample == 16);
        REQUIRE(r.peakLevel == Catch::Approx(0.8).margin(0.001));
        REQUIRE(r.rmsLevel == Catch::Approx(0.565685).margin(0.001));
        REQUIRE(r.spectralCentroid == Catch::Approx(440).margin(20));
        REQUIRE(r.spectralRolloff > 420);
        REQUIRE(r.spectralRolloff < 480);
        REQUIRE(r.spectralFlatness < 0.01);
    }
}
TEST_CASE("Silence has finite zero descriptors and short files are supported", "[analyzer]")
{
    TestDirectory t;
    auto r = BasicAnalyzer().analyze(t.wave("silent.wav", 0, 1, 100));
    REQUIRE(r.error.isEmpty());
    REQUIRE(r.peakLevel == 0);
    REQUIRE(r.rmsLevel == 0);
    REQUIRE(r.spectralCentroid == 0);
    REQUIRE(r.spectralRolloff == 0);
    REQUIRE(r.spectralFlatness == 0);
}
TEST_CASE("Analyzer reports corrupt files, missing files and cancellation explicitly", "[analyzer]")
{
    TestDirectory t;
    auto bad = t.dir.getChildFile("corrupt.wav"); bad.replaceWithText("broken");
    REQUIRE(BasicAnalyzer().analyze(bad).error.isNotEmpty());
    REQUIRE(BasicAnalyzer().analyze(t.dir.getChildFile("missing.wav")).error.isNotEmpty());
    REQUIRE(BasicAnalyzer().analyze(t.wave(), [] { return true; }).error == "Cancelled");
}
