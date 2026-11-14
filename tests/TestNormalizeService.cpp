#include <catch2/catch_test_macros.hpp>
#include "core/edit/NormalizeService.h"
#include "core/edit/AudioEditWriter.h"
#include "TestHelpers.h"
#include <cmath>

using namespace sonicator;

namespace
{
juce::File writeWav(const juce::File& file, const std::vector<float>& left,
                    const std::vector<float>& right = {})
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    const int channels = right.empty() ? 1 : 2;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(channels).withBitsPerSample(16)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = wav.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    juce::AudioBuffer<float> buffer(channels, (int) left.size());
    buffer.copyFrom(0, 0, left.data(), (int) left.size());
    if (channels > 1) buffer.copyFrom(1, 0, right.data(), (int) right.size());
    REQUIRE(writer->writeFromFloatArrays(buffer.getArrayOfReadPointers(), channels,
                                          (int) left.size()));
    writer.reset();
    stream.reset();
    return file;
}

struct Peaks
{
    float left = 0.0f, right = 0.0f;
    int channels = 0;
    int64_t frames = 0;
    int sampleRate = 0;
    bool ok = false;
};

Peaks readPeaks(const juce::File& file)
{
    Peaks peaks;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) return peaks;
    const int channels = (int) reader->numChannels;
    juce::AudioBuffer<float> buffer(channels, (int) reader->lengthInSamples);
    if (! reader->read(&buffer, 0, (int) reader->lengthInSamples, 0, true, true)) return peaks;
    peaks.left = buffer.getMagnitude(0, buffer.getNumSamples());
    peaks.right = channels > 1 ? buffer.getMagnitude(1, 0, buffer.getNumSamples()) : peaks.left;
    peaks.channels = channels;
    peaks.frames = reader->lengthInSamples;
    peaks.sampleRate = (int) reader->sampleRate;
    peaks.ok = true;
    return peaks;
}

juce::MemoryBlock readBytes(const juce::File& file)
{
    juce::MemoryBlock block;
    file.loadFileAsData(block);
    return block;
}
} // namespace

TEST_CASE("Normalize writes a copy and leaves the source untouched", "[normalize]")
{
    TestDirectory directory;
    const auto source = directory.wave("loud.wav", 440.0, 1, 4096);
    const auto before = readBytes(source);
    const auto result = normalizeFile(source, NormalizeOptions{ false });
    REQUIRE(result.state == NormalizeFileState::normalized);
    REQUIRE_FALSE(result.replacedOriginal);
    REQUIRE(result.destination != source);
    REQUIRE(result.destination.existsAsFile());
    REQUIRE(result.destination.getFileName() == "loud edit.wav");
    REQUIRE(readBytes(source) == before);
    const auto peaks = readPeaks(result.destination);
    REQUIRE(peaks.ok);
    REQUIRE(peaks.frames == 4096);
    REQUIRE(peaks.sampleRate == 44100);
    REQUIRE(std::abs(peaks.left - normalizePeakTarget) < 2.0e-4f);
}

TEST_CASE("Normalize replaces a writable original only after verification", "[normalize]")
{
    TestDirectory directory;
    const auto source = directory.wave("quiet.wav", 220.0, 2, 8192, true);
    REQUIRE(readPeaks(source).left < 0.9f);
    const auto result = normalizeFile(source, NormalizeOptions{ true });
    REQUIRE(result.state == NormalizeFileState::normalized);
    REQUIRE(result.replacedOriginal);
    REQUIRE(result.destination == source);
    const auto peaks = readPeaks(source);
    REQUIRE(peaks.ok);
    REQUIRE(peaks.channels == 2);
    REQUIRE(peaks.frames == 8192);
    // One coefficient for both channels: the inverted stereo fixture stays symmetric.
    REQUIRE(std::abs(peaks.left - normalizePeakTarget) < 2.0e-4f);
    REQUIRE(std::abs(peaks.right - normalizePeakTarget) < 2.0e-4f);
}

TEST_CASE("Normalize steps up when the copy name is taken", "[normalize]")
{
    TestDirectory directory;
    const auto source = directory.wave("loud.wav", 440.0, 1, 2048);
    directory.dir.getChildFile("loud edit.wav").replaceWithText("taken");
    const auto result = normalizeFile(source, NormalizeOptions{ false });
    REQUIRE(result.state == NormalizeFileState::normalized);
    REQUIRE(result.destination.getFileName() == "loud edit 2.wav");
    REQUIRE(directory.dir.getChildFile("loud edit.wav").loadFileAsString() == "taken");
}

TEST_CASE("Normalize skips silent files and files already on target", "[normalize]")
{
    TestDirectory directory;
    const auto silent = directory.wave("silent.wav", 0.0, 1, 2048);
    const auto silentBefore = readBytes(silent);
    const auto silentResult = normalizeFile(silent, NormalizeOptions{ true });
    REQUIRE(silentResult.state == NormalizeFileState::unchanged);
    REQUIRE(silentResult.error == "Silent source");
    REQUIRE(readBytes(silent) == silentBefore);

    const auto source = directory.wave("tone.wav", 330.0, 1, 4096);
    const auto first = normalizeFile(source, NormalizeOptions{ true });
    REQUIRE(first.state == NormalizeFileState::normalized);
    const auto onTarget = readBytes(source);
    const auto second = normalizeFile(source, NormalizeOptions{ true });
    REQUIRE(second.state == NormalizeFileState::unchanged);
    REQUIRE(second.error == "Already at -0.1 dBFS");
    REQUIRE(readBytes(source) == onTarget);
}

TEST_CASE("Normalize cancellation leaves the source unchanged", "[normalize]")
{
    TestDirectory directory;
    const auto source = directory.wave("tone.wav", 440.0, 1, 4096);
    const auto before = readBytes(source);
    const auto result = normalizeFile(source, NormalizeOptions{ true }, [] { return true; });
    REQUIRE(result.state == NormalizeFileState::cancelled);
    REQUIRE(readBytes(source) == before);
}

TEST_CASE("Normalize service reports every file of the batch", "[normalize]")
{
    TestDirectory directory;
    const auto first = directory.wave("one.wav", 220.0, 1, 4096);
    const auto second = directory.wave("two.wav", 0.0, 1, 4096);
    NormalizeService service;
    REQUIRE(service.start({ first, second }, NormalizeOptions{ true }));
    REQUIRE(service.waitUntilFinished(60000));
    const auto progress = service.progress();
    REQUIRE_FALSE(progress.running);
    REQUIRE(progress.total == 2);
    REQUIRE(progress.processed == 2);
    REQUIRE(progress.normalized == 1);
    REQUIRE(progress.unchanged == 1);
    const auto results = service.results();
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].state == NormalizeFileState::normalized);
    REQUIRE(results[1].source == second);
}
