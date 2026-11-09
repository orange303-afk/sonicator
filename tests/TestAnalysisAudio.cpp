#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/AnalysisAudio.h"
#include "core/dsp/RhythmAnalyzer.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace {
constexpr double rate = 44100.0;

AnalysisAudioOptions optionsForFrames(int64_t frames, int channels)
{
    AnalysisAudioOptions options;
    options.maxDecodedBytes = frames * 4 * (int64_t) channels;
    return options;
}
}

TEST_CASE("The shared reader decodes a WAV once", "[analysis-audio]")
{
    TestDirectory t;
    const auto file = t.wave("two.wav", 440.0, 1, (int) (2.0 * rate));
    const auto audio = readAnalysisAudio(file);
    REQUIRE(audio.ok());
    REQUIRE(audio.sampleRate == Catch::Approx(rate));
    REQUIRE(audio.buffer.getNumChannels() == 1);
    REQUIRE(audio.buffer.getNumSamples() == (int) (2.0 * rate));
    REQUIRE_FALSE(audio.truncated);
}

TEST_CASE("The shared reader honours the time cap", "[analysis-audio]")
{
    TestDirectory t;
    const auto file = t.wave("two.wav", 440.0, 1, (int) (2.0 * rate));
    AnalysisAudioOptions options;
    options.maxSeconds = 0.5;
    const auto audio = readAnalysisAudio(file, options);
    REQUIRE(audio.ok());
    REQUIRE(audio.buffer.getNumSamples() == (int) (0.5 * rate));
    REQUIRE(audio.truncated);
}

TEST_CASE("The shared reader honours the decoded-memory cap", "[analysis-audio]")
{
    TestDirectory t;
    const auto file = t.wave("stereo.wav", 440.0, 2, (int) (4.0 * rate));
    const auto audio = readAnalysisAudio(file, optionsForFrames(88200, 2));
    REQUIRE(audio.ok());
    REQUIRE(audio.buffer.getNumSamples() == 88200);
    REQUIRE(audio.truncated);
}

TEST_CASE("A non-audio file reports an error", "[analysis-audio]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("not-audio.wav");
    file.replaceWithText("this is not audio");
    const auto audio = readAnalysisAudio(file);
    REQUIRE_FALSE(audio.ok());
    REQUIRE(audio.error.isNotEmpty());
}

TEST_CASE("Cancellation stops the shared reader", "[analysis-audio]")
{
    TestDirectory t;
    const auto file = t.wave("two.wav", 440.0, 1, (int) (2.0 * rate));
    int calls = 0;
    const auto audio = readAnalysisAudio(file, {}, [&calls] { return ++calls >= 1; });
    REQUIRE_FALSE(audio.ok());
    REQUIRE(audio.error == "Cancelled");
}

TEST_CASE("The rhythm file reader keeps its truncation note", "[analysis-audio]")
{
    TestDirectory t;
    const auto file = t.wave("four.wav", 440.0, 1, (int) (4.0 * rate));
    RhythmOptions options;
    options.maxDecodedBytes = (int64_t) (rate * 2.0 * 4.0);
    const auto result = RhythmAnalyzer::analyzeFile(file, options);
    REQUIRE(result.durationSeconds == Catch::Approx(2.0).margin(0.01));
    REQUIRE(result.explanation.contains("first 2 s analysed"));
}
