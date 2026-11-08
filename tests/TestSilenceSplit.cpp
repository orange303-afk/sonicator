#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/split/SilenceDetector.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace
{
juce::File segments(const juce::File& file,
                    const std::vector<std::pair<int, bool>>& toneOrSilence,
                    double rate = 44100.0)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withNumChannels(1).withBitsPerSample(16));
    int total = 0;
    for (const auto& [frames, tone] : toneOrSilence) total += frames;
    juce::AudioBuffer<float> buffer(1, total);
    buffer.clear();
    int pos = 0;
    for (const auto& [frames, tone] : toneOrSilence)
    {
        if (tone)
            for (int n = 0; n < frames; ++n, ++pos)
                buffer.setSample(0, pos, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi
                    * 220.0f * (float) pos / 44100.0f));
        else
            pos += frames;
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, total);
    writer.reset();
    return file;
}
}

TEST_CASE("Detector splits tone bursts at two-second silence", "[split][detector]")
{
    TestDirectory t;
    auto file = segments(t.dir.getChildFile("a.wav"),
        { { 44100, true }, { 88200, false }, { 44100, true },
          { 88200, false }, { 44100, true }, { 88200, false } });
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, {});
    REQUIRE(result.parts.size() == 3);
    REQUIRE(result.parts[0].startSeconds == Catch::Approx(0.0).margin(0.01));
    REQUIRE(result.parts[0].endSeconds == Catch::Approx(1.0).margin(0.01));
    REQUIRE(result.parts[1].startSeconds == Catch::Approx(3.0).margin(0.01));
    REQUIRE(result.parts[2].endSeconds == Catch::Approx(7.0).margin(0.01));
    REQUIRE(result.discardedShortParts == 0);
}

TEST_CASE("Detector ignores short gaps and trims file edges", "[split][detector]")
{
    TestDirectory t;
    // 0.5 s lead, 1 s tone, 100 ms gap (below 250 ms), 1 s tone, 0.5 s tail
    auto file = segments(t.dir.getChildFile("b.wav"),
        { { 22050, false }, { 44100, true }, { 4410, false }, { 44100, true }, { 22050, false } });
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, {});
    REQUIRE(result.parts.size() == 1);
    REQUIRE(result.parts[0].startSeconds == Catch::Approx(0.5).margin(0.01));
    REQUIRE(result.parts[0].endSeconds == Catch::Approx(2.6).margin(0.01));
}

TEST_CASE("Detector counts a dropped short fragment", "[split][detector]")
{
    TestDirectory t;
    // 1 s tone, 0.5 s gap, 50 ms blip, 0.5 s gap, 1 s tone
    auto file = segments(t.dir.getChildFile("c.wav"),
        { { 44100, true }, { 22050, false }, { 2205, true }, { 22050, false }, { 44100, true } });
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, {});
    REQUIRE(result.parts.size() == 2);
    REQUIRE(result.discardedShortParts == 1);
}

TEST_CASE("Detector stops on cancellation", "[split][detector]")
{
    TestDirectory t;
    auto file = segments(t.dir.getChildFile("d.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true } });
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, [] { return true; });
    REQUIRE(result.parts.empty());
}
