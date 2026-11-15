#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/batch/BatchPipeline.h"
#include "core/audio/SonicatorFormats.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
constexpr double sampleRate = 44100.0;

juce::File floatWav(const juce::File& file, const std::vector<std::vector<float>>& channels)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(sampleRate)
        .withNumChannels((int) channels.size()).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    std::vector<const float*> pointers;
    for (const auto& channel : channels) pointers.push_back(channel.data());
    REQUIRE(writer->writeFromFloatArrays(pointers.data(), (int) pointers.size(),
                                         (int) channels.front().size()));
    writer.reset();
    stream.reset();
    return file;
}

SourceSnapshot snapshotFor(const juce::File& file)
{
    SourceSnapshot snapshot;
    snapshot.path = file;
    snapshot.sampleId = 1;
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
    snapshot.fileSize = file.getSize();
    snapshot.modifiedMs = file.getLastModificationTime().toMilliseconds();
    return snapshot;
}

std::unique_ptr<PreparedAudio> render(const juce::File& file, const Recipe& recipe)
{
    auto prepared = prepareAudio(snapshotFor(file), recipe);
    REQUIRE(prepared.ok);
    REQUIRE(prepared.audio != nullptr);
    return std::move(prepared.audio);
}

std::vector<float> readAll(PreparedAudio& audio, int blockSize = 64)
{
    std::vector<float> out;
    juce::AudioBuffer<float> block(audio.channels(), blockSize);
    int64_t position = 0;
    while (position < audio.frames())
    {
        const auto result = audio.readFrames(position,
            (int) std::min<int64_t>(blockSize, audio.frames() - position), block);
        REQUIRE(result.state == ReadState::ok);
        REQUIRE(result.frames > 0);
        for (int n = 0; n < result.frames; ++n) out.push_back(block.getSample(0, n));
        position += result.frames;
    }
    return out;
}

std::vector<std::vector<float>> readChannels(PreparedAudio& audio)
{
    std::vector<std::vector<float>> out((size_t) audio.channels());
    juce::AudioBuffer<float> block(audio.channels(), 64);
    int64_t position = 0;
    while (position < audio.frames())
    {
        const auto result = audio.readFrames(position,
            (int) std::min<int64_t>(64, audio.frames() - position), block);
        REQUIRE(result.state == ReadState::ok);
        for (int channel = 0; channel < audio.channels(); ++channel)
            for (int n = 0; n < result.frames; ++n)
                out[(size_t) channel].push_back(block.getSample(channel, n));
        position += result.frames;
    }
    return out;
}

Recipe oneStep(StepParameters parameters)
{
    Recipe recipe;
    recipe.steps.push_back({ true, std::move(parameters) });
    return recipe;
}

double msForFrames(int64_t frames) { return (double) frames * 1000.0 / sampleRate; }
} // namespace

TEST_CASE("Remove DC subtracts the per-channel mean of the prepared stream",
          "[batch][batch-pipeline]")
{
    TestDirectory t;
    const auto mono = floatWav(t.dir.getChildFile("dc.wav"), { { 0.25f, 0.5f, 0.75f } });
    auto audio = render(mono, oneStep(RemoveDc {}));
    const auto values = readAll(*audio);
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == Catch::Approx(-0.25f).margin(1e-6));
    REQUIRE(values[1] == Catch::Approx(0.0f).margin(1e-6));
    REQUIRE(values[2] == Catch::Approx(0.25f).margin(1e-6));

    const auto stereo = floatWav(t.dir.getChildFile("dc-stereo.wav"),
                                 { { 0.1f, 0.3f }, { -0.2f, 0.2f } });
    auto stereoAudio = render(stereo, oneStep(RemoveDc {}));
    const auto channels = readChannels(*stereoAudio);
    REQUIRE(channels[0][0] == Catch::Approx(-0.1f).margin(1e-6));
    REQUIRE(channels[0][1] == Catch::Approx(0.1f).margin(1e-6));
    REQUIRE(channels[1][0] == Catch::Approx(-0.2f).margin(1e-6));
    REQUIRE(channels[1][1] == Catch::Approx(0.2f).margin(1e-6));
}

TEST_CASE("Trim silence keeps the sound span, all-silent length and either stereo channel",
          "[batch][batch-pipeline]")
{
    TestDirectory t;
    const auto file = floatWav(t.dir.getChildFile("trim.wav"), { { 0.0f, 0.0f, 0.5f, 0.0f } });
    TrimSilence trim; trim.thresholdDb = -60.0; trim.paddingMs = 0.0;
    auto audio = render(file, oneStep(trim));
    REQUIRE(audio->frames() == 1);
    REQUIRE(readAll(*audio)[0] == Catch::Approx(0.5f));

    const auto silent = floatWav(t.dir.getChildFile("silent.wav"), { { 0.0f, 0.0f, 0.0f, 0.0f } });
    auto silentAudio = render(silent, oneStep(trim));
    REQUIRE(silentAudio->frames() == 4);
    REQUIRE(silentAudio->stepOutcomes().front().changed == false);

    const auto right = floatWav(t.dir.getChildFile("right.wav"),
                                { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.5f, 0.0f, 0.0f } });
    auto rightAudio = render(right, oneStep(trim));
    REQUIRE(rightAudio->frames() == 1);
    REQUIRE(readChannels(*rightAudio)[1][0] == Catch::Approx(0.5f));
}

TEST_CASE("Later steps measure the trimmed and faded intermediate stream",
          "[batch][batch-pipeline]")
{
    TestDirectory t;
    // Trim then fade keeps four frames; fade then trim drops the silent last frame.
    const auto file = floatWav(t.dir.getChildFile("order.wav"), { { 1.0f, 1.0f, 1.0f, 1.0f } });
    FadeOut fade; fade.ms = msForFrames(4);
    TrimSilence trim; trim.thresholdDb = -60.0; trim.paddingMs = 0.0;
    Recipe trimThenFade;
    trimThenFade.steps = { { true, trim }, { true, fade } };
    auto first = render(file, trimThenFade);
    REQUIRE(first->frames() == 4);
    auto values = readAll(*first);
    REQUIRE(values[0] == Catch::Approx(1.0f));
    REQUIRE(values[3] == Catch::Approx(0.0f).margin(1e-6));

    Recipe fadeThenTrim;
    fadeThenTrim.steps = { { true, fade }, { true, trim } };
    auto second = render(file, fadeThenTrim);
    REQUIRE(second->frames() == 3);
}

TEST_CASE("Normalize reaches the requested peak and Gain order changes the result",
          "[batch][batch-pipeline]")
{
    TestDirectory t;
    const auto file = floatWav(t.dir.getChildFile("gain.wav"), { { 0.25f, -0.5f } });
    Normalize normalize; normalize.peakDb = 0.0;
    auto normalized = render(file, oneStep(normalize));
    auto values = readAll(*normalized);
    REQUIRE(values[0] == Catch::Approx(0.5f).margin(1e-5));
    REQUIRE(values[1] == Catch::Approx(-1.0f).margin(1e-5));

    Gain gain; gain.gainDb = 6.020599913279624;
    Recipe gainThenNormalize;
    gainThenNormalize.steps = { { true, gain }, { true, normalize } };
    auto a = render(file, gainThenNormalize);
    const auto aValues = readAll(*a);
    Recipe normalizeThenGain;
    normalizeThenGain.steps = { { true, normalize }, { true, gain } };
    auto b = render(file, normalizeThenGain);
    const auto bValues = readAll(*b);
    REQUIRE(aValues[1] == Catch::Approx(-1.0f).margin(1e-5));
    REQUIRE(bValues[1] == Catch::Approx(-2.0f).margin(1e-4));
    REQUIRE(std::abs(aValues[1] - bValues[1]) > 0.5);
}

TEST_CASE("Fades use explicit endpoints, block boundaries and overlap",
          "[batch][batch-pipeline]")
{
    TestDirectory t;
    const auto file = floatWav(t.dir.getChildFile("fade.wav"), { { 1.0f, 1.0f, 1.0f } });
    FadeIn fadeIn; fadeIn.ms = msForFrames(3);
    auto in = render(file, oneStep(fadeIn));
    auto values = readAll(*in);
    REQUIRE(values[0] == Catch::Approx(0.0f));
    REQUIRE(values[1] == Catch::Approx(0.5f));
    REQUIRE(values[2] == Catch::Approx(1.0f));

    FadeOut fadeOut; fadeOut.ms = msForFrames(3);
    auto out = render(file, oneStep(fadeOut));
    values = readAll(*out);
    REQUIRE(values[0] == Catch::Approx(1.0f));
    REQUIRE(values[1] == Catch::Approx(0.5f));
    REQUIRE(values[2] == Catch::Approx(0.0f));

    FadeIn shortFade; shortFade.ms = 0.0;
    auto unchanged = render(file, oneStep(shortFade));
    REQUIRE(unchanged->frames() == 3);
    REQUIRE(readAll(*unchanged) == std::vector<float> { 1.0f, 1.0f, 1.0f });
}

TEST_CASE("Repeated reads and different block sizes have identical semantics",
          "[batch][batch-pipeline]")
{
    TestDirectory t;
    std::vector<float> samples(1000);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = (float) std::sin((double) i * 0.05) * 0.5f;
    const auto file = floatWav(t.dir.getChildFile("block.wav"), { samples });
    Recipe recipe = oneStep(Gain { 3.0 });
    auto audio = render(file, recipe);
    const auto one = readAll(*audio, 1);
    const auto many = readAll(*audio, 257);
    const auto whole = readAll(*audio, 16384);
    REQUIRE(one == many);
    REQUIRE(one == whole);

    juce::AudioBuffer<float> block(1, 32);
    const auto first = audio->readFrames(10, 32, block);
    REQUIRE(first.frames == 32);
    std::vector<float> firstValues(block.getReadPointer(0), block.getReadPointer(0) + 32);
    const auto again = audio->readFrames(10, 32, block);
    REQUIRE(again.frames == 32);
    for (int i = 0; i < 32; ++i)
        REQUIRE(block.getSample(0, i) == Catch::Approx(firstValues[(size_t) i]));

    float peakValue = 0.0f;
    REQUIRE(audio->peak(peakValue).state == ReadState::ok);
    REQUIRE(peakValue > 0.0f);
}

TEST_CASE("Fade coefficients match the documented formulas", "[batch][batch-pipeline]")
{
    REQUIRE(fadeCoefficient(0, 3, 3, false, false) == Catch::Approx(0.0f));
    REQUIRE(fadeCoefficient(1, 3, 3, false, false) == Catch::Approx(0.5f));
    REQUIRE(fadeCoefficient(2, 3, 3, false, false) == Catch::Approx(1.0f));
    REQUIRE(fadeCoefficient(0, 3, 3, true, false) == Catch::Approx(1.0f));
    REQUIRE(fadeCoefficient(2, 3, 3, true, false) == Catch::Approx(0.0f));
    REQUIRE(fadeCoefficient(1, 3, 3, false, true) == Catch::Approx(0.5f));
    REQUIRE(fadeCoefficient(5, 10, 0, false, false) == Catch::Approx(1.0f));
}

TEST_CASE("A prepared stream honours cancellation even while a trim drops a long prefix",
          "[batch][batch-pipeline]")
{
    TestDirectory t;
    std::vector<float> samples(500000, 0.0f);
    samples.back() = 0.5f;
    const auto file = floatWav(t.dir.getChildFile("prefix.wav"), { samples });
    TrimSilence trim; trim.thresholdDb = -60.0; trim.paddingMs = 0.0;
    bool cancelNow = false;
    const auto snapshot = snapshotFor(file);
    auto prepared = prepareAudio(snapshot, oneStep(trim), [&cancelNow] { return cancelNow; });
    REQUIRE(prepared.ok);
    cancelNow = true;
    juce::AudioBuffer<float> block(1, 64);
    const auto read = prepared.audio->readFrames(0, 64, block);
    REQUIRE(read.state == ReadState::cancelled);
}
