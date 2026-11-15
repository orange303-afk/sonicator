#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/batch/BatchPreviewService.h"
#include "core/audio/SonicatorFormats.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
juce::File floatWav(const juce::File& file, const std::vector<float>& samples, int rate = 44100)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate((double) rate).withNumChannels(1)
        .withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    const float* channels[1] { samples.data() };
    REQUIRE(writer->writeFromFloatArrays(channels, 1, (int) samples.size()));
    writer.reset();
    stream.reset();
    return file;
}

SourceSnapshot snapshotFor(const juce::File& file)
{
    SourceSnapshot snapshot;
    snapshot.path = file;
    snapshot.identity = physicalIdentity(file);
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    snapshot.sampleRate = (int) reader->sampleRate;
    snapshot.channels = (int) reader->numChannels;
    snapshot.bits = (int) reader->bitsPerSample;
    snapshot.floating = reader->usesFloatingPointData;
    snapshot.frames = reader->lengthInSamples;
    return snapshot;
}

std::vector<float> readWav(const juce::File& file, int& rate, int64_t& frames)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    rate = (int) reader->sampleRate;
    frames = reader->lengthInSamples;
    juce::AudioBuffer<float> buffer((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    std::vector<float> out((size_t) reader->lengthInSamples);
    for (int i = 0; i < (int) reader->lengthInSamples; ++i) out[(size_t) i] = buffer.getSample(0, i);
    return out;
}
} // namespace

TEST_CASE("Preview normalization uses whole-file statistics, not the first ten seconds",
          "[batch][batch-preview]")
{
    TestDirectory t;
    std::vector<float> samples((size_t) 44100 * 20);
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const bool firstHalf = i < (size_t) 44100 * 10;
        samples[i] = (float) std::sin((double) i * 0.05) * (firstHalf ? 0.1f : 0.5f);
    }
    const auto file = floatWav(t.dir.getChildFile("global.wav"), samples);
    Recipe recipe;
    recipe.steps = { { true, Normalize { -6.0 } } };
    BatchPreviewService service;
    REQUIRE(service.start(snapshotFor(file), recipe, 1, t.dir.getChildFile("Preview")));
    REQUIRE(service.waitUntilFinished(60000));
    const auto result = service.takeResult();
    REQUIRE(result.ok);
    REQUIRE(result.generation == 1);
    REQUIRE(result.dry.existsAsFile());
    REQUIRE(result.wet.existsAsFile());
    // The whole file peaks at 0.5, so -6 dBFS leaves the quiet first ten seconds near 0.1.
    REQUIRE(result.wetInfo.peak < 0.2f);
    REQUIRE(result.dryInfo.frames == 44100 * 10);
    REQUIRE(result.wetInfo.frames <= (int64_t) 44100 * 10);
}

TEST_CASE("Preview starts at the first frame that survives Trim", "[batch][batch-preview]")
{
    TestDirectory t;
    std::vector<float> samples(1000, 0.0f);
    samples.resize(1140, 0.5f);
    samples.resize(2000, 0.0f);
    const auto file = floatWav(t.dir.getChildFile("trim.wav"), samples);
    Recipe recipe;
    recipe.steps = { { true, TrimSilence { -60.0, 0.0 } } };
    BatchPreviewService service;
    REQUIRE(service.start(snapshotFor(file), recipe, 3, t.dir.getChildFile("Preview")));
    REQUIRE(service.waitUntilFinished(60000));
    const auto result = service.takeResult();
    REQUIRE(result.ok);
    REQUIRE(result.generation == 3);
    REQUIRE(result.sourceStartFrame == 1000);
    int dryRate = 0, wetRate = 0;
    int64_t dryFrames = 0, wetFrames = 0;
    const auto dry = readWav(result.dry, dryRate, dryFrames);
    const auto wet = readWav(result.wet, wetRate, wetFrames);
    REQUIRE(dry.at(0) == Catch::Approx(0.5f));
    REQUIRE(wet.at(0) == Catch::Approx(0.5f));
    REQUIRE(dryFrames == 1000); // dry covers the same start for up to ten seconds
    REQUIRE(wetFrames >= 140);
}

TEST_CASE("Preview output follows the requested sample rate and stays under ten seconds",
          "[batch][batch-preview]")
{
    TestDirectory t;
    std::vector<float> samples((size_t) 44100 * 20, 0.25f);
    const auto file = floatWav(t.dir.getChildFile("rate.wav"), samples);
    Recipe recipe;
    recipe.sampleRate = 48000;
    BatchPreviewService service;
    REQUIRE(service.start(snapshotFor(file), recipe, 4, t.dir.getChildFile("Preview")));
    REQUIRE(service.waitUntilFinished(60000));
    const auto result = service.takeResult();
    REQUIRE(result.ok);
    int wetRate = 0;
    int64_t wetFrames = 0;
    readWav(result.wet, wetRate, wetFrames);
    REQUIRE(wetRate == 48000);
    REQUIRE(wetFrames == 48000 * 10);
}

TEST_CASE("Cancelling a preview removes its own files and reports the cancellation",
          "[batch][batch-preview]")
{
    TestDirectory t;
    std::vector<float> samples((size_t) 44100 * 20);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = (float) std::sin((double) i * 0.01) * 0.4f;
    const auto file = floatWav(t.dir.getChildFile("cancel.wav"), samples);
    Recipe recipe;
    recipe.steps = { { true, RemoveDc {} }, { true, Normalize { -0.1 } } };
    const auto folder = t.dir.getChildFile("Preview");
    BatchPreviewService service;
    REQUIRE(service.start(snapshotFor(file), recipe, 5, folder));
    service.cancel();
    REQUIRE(service.waitUntilFinished(60000));
    const auto result = service.takeResult();
    REQUIRE(result.cancelled);
    REQUIRE_FALSE(result.ok);
    int files = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles))
    {
        juce::ignoreUnused(entry);
        ++files;
    }
    REQUIRE(files == 0);
}
