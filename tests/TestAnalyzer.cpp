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
        REQUIRE(r.version == basicFeaturesVersion);
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
#if JUCE_MAC || JUCE_LINUX
TEST_CASE("A hashing open failure becomes a per-file error after successful decoding", "[analyzer]")
{
    TestDirectory t; auto file = t.wave(); bool removed = false;
    auto r = BasicAnalyzer().analyzeAndHash(file,[&]
    {
        if (! removed) { REQUIRE(file.deleteFile()); removed = true; }
        return false; // Existing decoder stream remains readable on POSIX, hashing reopen fails.
    });
    REQUIRE(removed); REQUIRE(r.error.isNotEmpty()); REQUIRE(r.version == 0);
    REQUIRE(BasicAnalyzer().analyzeAndHash(t.wave("healthy.wav")).error.isEmpty());
}
#endif
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
TEST_CASE("Intact headers with truncated WAV or AIFF payloads are not cached as successful", "[analyzer]")
{
    TestDirectory t;
    auto wav = t.wave("truncated.wav",440,1,100);
    {
        juce::FileOutputStream stream(wav); stream.setPosition(64); REQUIRE(stream.truncate().wasOk());
    }
    auto r = BasicAnalyzer().analyze(wav);
    REQUIRE(r.error.isNotEmpty()); REQUIRE(r.version == 0);
    // Even a patched outer RIFF size must not hide a too-large inner data chunk.
    {
        juce::FileOutputStream stream(wav); stream.setPosition(4); stream.writeInt(56);
    }
    REQUIRE(BasicAnalyzer().analyze(wav).error.isNotEmpty());
    auto aiff = t.dir.getChildFile("truncated.aiff");
    REQUIRE(juce::File(SONICATOR_FIXTURE_DIR).getChildFile("tone.aiff").copyFileTo(aiff));
    {
        juce::FileOutputStream stream(aiff); stream.setPosition(64); REQUIRE(stream.truncate().wasOk());
    }
    REQUIRE(BasicAnalyzer().analyze(aiff).error.isNotEmpty());
}
TEST_CASE("Complete audio remains readable with inaccurate outer RIFF sizes", "[analyzer]")
{
    TestDirectory t; auto file = t.wave();
    {
        juce::FileOutputStream stream(file); stream.setPosition(4);
        stream.writeInt((int) file.getSize()); // A common exporter bug: includes the 8-byte RIFF header.
    }
    auto result = BasicAnalyzer().analyze(file);
    REQUIRE(result.error.isEmpty()); REQUIRE(result.duration == 1);
    REQUIRE(result.rmsLevel == Catch::Approx(0.565685).margin(0.001));
}
