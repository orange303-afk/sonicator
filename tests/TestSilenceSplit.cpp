#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/split/SilenceDetector.h"
#include "core/split/SplitService.h"
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

TEST_CASE("Split service writes named parts, keeps originals and never overwrites", "[split][service]")
{
    TestDirectory t;
    auto source = segments(t.dir.getChildFile("loop.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true },
          { 44100, false }, { 44100, true } });
    t.dir.getChildFile("loop_01.wav").replaceWithText("occupied");
    SplitService service;
    SplitOptions options;
    REQUIRE(service.start({ source }, options));
    REQUIRE(service.waitUntilFinished(20000));
    auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == SplitFileState::split);
    REQUIRE(results[0].deletable());
    REQUIRE(results[0].parts.size() == 3);
    REQUIRE(source.existsAsFile());
    REQUIRE(t.dir.getChildFile("loop_01.wav").loadFileAsString() == "occupied");
    REQUIRE(t.dir.getChildFile("loop_01-1.wav").existsAsFile());
    REQUIRE(t.dir.getChildFile("loop_02.wav").existsAsFile());
    REQUIRE(t.dir.getChildFile("loop_03.wav").existsAsFile());
}

TEST_CASE("A bad file does not stop the batch and dropped fragments block deletion", "[split][service]")
{
    TestDirectory t;
    auto good = segments(t.dir.getChildFile("good.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true } });
    auto blip = segments(t.dir.getChildFile("blip.wav"),
        { { 44100, true }, { 22050, false }, { 2205, true },
          { 22050, false }, { 44100, true } });
    auto missing = t.dir.getChildFile("missing.wav");
    SplitService service;
    REQUIRE(service.start({ good, missing, blip }, SplitOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    auto results = service.results();
    REQUIRE(results.size() == 3);
    REQUIRE(results[0].state == SplitFileState::split);
    REQUIRE(results[0].deletable());
    REQUIRE(results[1].state == SplitFileState::skipped);
    REQUIRE_FALSE(results[1].deletable());
    REQUIRE(results[2].discardedShortParts == 1);
    REQUIRE_FALSE(results[2].deletable());
}

TEST_CASE("No-silence sources and unusable output folders are reported, not deleted", "[split][service]")
{
    TestDirectory t;
    auto continuous = segments(t.dir.getChildFile("continuous.wav"), { { 132300, true } });
    SplitService service;
    REQUIRE(service.start({ continuous }, SplitOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == SplitFileState::noSplit);
    REQUIRE(results[0].parts.empty());
    REQUIRE(continuous.existsAsFile());

    auto source = segments(t.dir.getChildFile("tone.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true } });
    SplitOptions bad; bad.sameFolder = false; bad.outputFolder = t.dir.getChildFile("not-a-folder.wav");
    bad.outputFolder.replaceWithText("x");
    SplitService second;
    REQUIRE(second.start({ source }, bad));
    REQUIRE(second.waitUntilFinished(20000));
    results = second.results();
    REQUIRE(results[0].state == SplitFileState::failed);
    REQUIRE_FALSE(results[0].deletable());
    REQUIRE(source.existsAsFile());
}

TEST_CASE("Only a complete split is deletable", "[split][service]")
{
    SplitFileResult r;
    r.state = SplitFileState::split;
    r.detectedParts = 2; r.parts = { juce::File("/tmp/a_01.wav"), juce::File("/tmp/a_02.wav") };
    REQUIRE(r.deletable());
    r.discardedShortParts = 1;
    REQUIRE_FALSE(r.deletable());
    r.discardedShortParts = 0; r.state = SplitFileState::cancelled;
    REQUIRE_FALSE(r.deletable());
    r.state = SplitFileState::failed;
    REQUIRE_FALSE(r.deletable());
    r.state = SplitFileState::noSplit;
    REQUIRE_FALSE(r.deletable());
}
