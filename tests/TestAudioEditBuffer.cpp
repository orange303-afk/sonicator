#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/edit/AudioEditBuffer.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::edit;
using Catch::Approx;

namespace
{
AudioEditBuffer rampBuffer(int64_t frames, int channels = 1)
{
    SampleAudio audio;
    audio.sampleRate = 44100;
    audio.channels = channels;
    audio.frames = frames;
    audio.left.resize((size_t) frames);
    for (int64_t i = 0; i < frames; ++i) audio.left[(size_t) i] = (float) i;
    if (channels > 1)
    {
        audio.right.resize((size_t) frames);
        for (int64_t i = 0; i < frames; ++i) audio.right[(size_t) i] = (float) i;
    }
    return AudioEditBuffer::fromSampleAudio(audio);
}

juce::File writeWav(const juce::File& file, int channels, int frames)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(channels).withBitsPerSample(16);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f
                                                   * (float) i / 44100.0f));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}
} // namespace

TEST_CASE("Edit buffer loads a stereo WAV with its frame count", "[edit]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("stereo.wav"), 2, 1000);
    juce::String error;
    auto buffer = AudioEditBuffer::load(file, error);
    REQUIRE(buffer.has_value());
    REQUIRE(buffer->channels() == 2);
    REQUIRE(buffer->sampleRate() == 44100);
    REQUIRE(buffer->frames() == 1000);
    REQUIRE((int64_t) buffer->channel(0).size() == buffer->frames());
    REQUIRE((int64_t) buffer->channel(1).size() == buffer->frames());
    REQUIRE_FALSE(buffer->isModified());
}

TEST_CASE("Delete region removes frames and shifts the tail", "[edit]")
{
    auto buffer = rampBuffer(1000);
    REQUIRE(buffer.apply({ EditOperation::deleteRegion, { 100, 200 } }));
    REQUIRE(buffer.frames() == 900);
    REQUIRE(buffer.channel(0)[100] == Approx(200.0f));
    REQUIRE(buffer.undo());
    REQUIRE(buffer.frames() == 1000);
    REQUIRE(buffer.channel(0)[100] == Approx(100.0f));
}

TEST_CASE("Crop keeps only the selection", "[edit]")
{
    auto buffer = rampBuffer(1000);
    REQUIRE(buffer.apply({ EditOperation::cropToSelection, { 100, 200 } }));
    REQUIRE(buffer.frames() == 100);
    REQUIRE(buffer.channel(0)[0] == Approx(100.0f));
}

TEST_CASE("Silence region zeroes frames without changing the length", "[edit]")
{
    auto buffer = rampBuffer(100);
    REQUIRE(buffer.apply({ EditOperation::silenceRegion, { 10, 20 } }));
    REQUIRE(buffer.frames() == 100);
    for (int64_t i = 10; i < 20; ++i) REQUIRE(buffer.channel(0)[(size_t) i] == 0.0f);
    REQUIRE(buffer.channel(0)[20] == Approx(20.0f));
}

TEST_CASE("Reverse mirrors the range and leaves the rest alone", "[edit]")
{
    auto buffer = rampBuffer(6);
    REQUIRE(buffer.apply({ EditOperation::reverse, { 0, 5 } }));
    REQUIRE(buffer.channel(0)[0] == Approx(4.0f));
    REQUIRE(buffer.channel(0)[1] == Approx(3.0f));
    REQUIRE(buffer.channel(0)[2] == Approx(2.0f));
    REQUIRE(buffer.channel(0)[3] == Approx(1.0f));
    REQUIRE(buffer.channel(0)[4] == Approx(0.0f));
    REQUIRE(buffer.channel(0)[5] == Approx(5.0f));
}

TEST_CASE("Undo and redo walk the history", "[edit]")
{
    auto buffer = rampBuffer(100);
    REQUIRE(buffer.apply({ EditOperation::silenceRegion, { 0, 10 } }));
    REQUIRE(buffer.apply({ EditOperation::silenceRegion, { 10, 20 } }));
    REQUIRE(buffer.isModified());
    REQUIRE(buffer.undo());
    REQUIRE(buffer.channel(0)[10] == Approx(10.0f));
    REQUIRE(buffer.isModified());
    REQUIRE(buffer.redo());
    REQUIRE(buffer.channel(0)[10] == 0.0f);
    REQUIRE(buffer.undo());
    REQUIRE(buffer.undo());
    REQUIRE_FALSE(buffer.isModified());
    REQUIRE_FALSE(buffer.undo());
}

TEST_CASE("Resolve uses the whole buffer when the selection is empty", "[edit]")
{
    auto buffer = rampBuffer(50);
    const auto whole = buffer.whole();
    const auto resolved = buffer.resolve({});
    REQUIRE(resolved.start == whole.start);
    REQUIRE(resolved.end == whole.end);
}

TEST_CASE("More than two channels is refused", "[edit]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("three.wav"), 3, 100);
    juce::String error;
    auto buffer = AudioEditBuffer::load(file, error);
    REQUIRE_FALSE(buffer.has_value());
    REQUIRE(error.isNotEmpty());
}

TEST_CASE("Undo memory stays under its cap", "[edit]")
{
    auto buffer = rampBuffer(5000000, 2);
    for (int i = 0; i < 20; ++i)
        REQUIRE(buffer.apply({ EditOperation::reverse, { 0, 100 + i } }));
    REQUIRE(buffer.historyBytes() <= (size_t) 256 * 1024 * 1024);
    REQUIRE(buffer.canUndo());
    REQUIRE(buffer.undo());
}

namespace
{
AudioEditBuffer bufferWith(std::vector<float> values, std::vector<float> second = {})
{
    SampleAudio audio;
    audio.sampleRate = 44100;
    audio.channels = second.empty() ? 1 : 2;
    audio.frames = (int64_t) values.size();
    audio.left = std::move(values);
    audio.right = std::move(second);
    return AudioEditBuffer::fromSampleAudio(audio);
}
} // namespace

TEST_CASE("Fade in ramps from zero to one", "[edit]")
{
    auto buffer = bufferWith(std::vector<float>(101, 1.0f));
    REQUIRE(buffer.apply({ EditOperation::fadeIn, { 0, 100 } }));
    REQUIRE(buffer.channel(0)[0] == Approx(0.0f));
    REQUIRE(buffer.channel(0)[99] == Approx(0.99f));
    REQUIRE(buffer.channel(0)[100] == Approx(1.0f));
}

TEST_CASE("Fade out ramps from one to zero", "[edit]")
{
    auto buffer = bufferWith(std::vector<float>(101, 1.0f));
    REQUIRE(buffer.apply({ EditOperation::fadeOut, { 0, 100 } }));
    REQUIRE(buffer.channel(0)[0] == Approx(1.0f));
    REQUIRE(buffer.channel(0)[99] == Approx(0.01f));
}

TEST_CASE("Normalize scales the peak to -0.1 dBFS", "[edit]")
{
    std::vector<float> left(100, 0.1f), right(100, 0.05f);
    left[42] = -0.1f;
    right[42] = -0.05f;
    auto buffer = bufferWith(left, right);
    REQUIRE(buffer.apply({ EditOperation::normalize, {} }));
    float peak = 0.0f;
    for (int i = 0; i < 100; ++i)
        peak = juce::jmax(peak, std::abs(buffer.channel(0)[(size_t) i]));
    REQUIRE(peak == Approx(0.9885531f).margin(1e-4));
    REQUIRE(buffer.channel(1)[0] / buffer.channel(0)[0] == Approx(0.5f));
}

TEST_CASE("Gain applies decibels to the range only", "[edit]")
{
    auto buffer = bufferWith(std::vector<float>(100, 1.0f));
    REQUIRE(buffer.apply({ EditOperation::gain, { 0, 10 }, 6.0f }));
    REQUIRE(buffer.channel(0)[0] == Approx(1.9953f).margin(1e-3));
    REQUIRE(buffer.channel(0)[10] == Approx(1.0f));
}

TEST_CASE("Convert to mono averages both channels", "[edit]")
{
    auto buffer = bufferWith(std::vector<float>(10, 0.5f), std::vector<float>(10, -0.5f));
    REQUIRE(buffer.apply({ EditOperation::toMono, {} }));
    REQUIRE(buffer.channels() == 1);
    REQUIRE(buffer.channel(0)[0] == Approx(0.0f));
    REQUIRE(buffer.channel(1).empty());

    auto mono = bufferWith(std::vector<float>(10, 0.5f));
    REQUIRE_FALSE(mono.apply({ EditOperation::toMono, {} }));
    REQUIRE_FALSE(mono.isModified());
}

TEST_CASE("Remove DC offset subtracts each channel mean", "[edit]")
{
    std::vector<float> left(1000), right(1000);
    for (int i = 0; i < 1000; ++i)
    {
        left[(size_t) i] = 0.25f + 0.1f * std::sin(0.01f * (float) i);
        right[(size_t) i] = -0.25f + 0.1f * std::sin(0.01f * (float) i);
    }
    auto buffer = bufferWith(left, right);
    REQUIRE(buffer.apply({ EditOperation::removeDcOffset, {} }));
    for (int channel = 0; channel < 2; ++channel)
    {
        double sum = 0;
        for (int i = 0; i < 1000; ++i) sum += buffer.channel(channel)[(size_t) i];
        REQUIRE(std::abs(sum / 1000.0) < 1e-4);
    }
}

TEST_CASE("Trim silence keeps five milliseconds of margin", "[edit]")
{
    std::vector<float> values(1000 + 44100 + 1000, 0.0f);
    for (int i = 1000; i < 1000 + 44100; ++i) values[(size_t) i] = 0.5f;
    auto buffer = bufferWith(values);
    REQUIRE(buffer.apply({ EditOperation::trimSilence, {} }));
    REQUIRE(buffer.frames() >= 44100 + 2 * 220);
    REQUIRE(buffer.frames() <= 44100 + 2 * 222);
    // The kept margin is silent by definition: the first audible frame follows it.
    REQUIRE(buffer.channel(0)[0] == Approx(0.0f));
    REQUIRE(buffer.channel(0)[221] == Approx(0.5f));
}

TEST_CASE("Trim silence on fully silent audio leaves it unchanged", "[edit]")
{
    auto buffer = bufferWith(std::vector<float>(1000, 0.0f));
    REQUIRE_FALSE(buffer.apply({ EditOperation::trimSilence, {} }));
    REQUIRE(buffer.frames() == 1000);
}

TEST_CASE("An edit stays modified after the base snapshot is evicted", "[edit]")
{
    auto buffer = rampBuffer(1000);
    buffer.setHistoryLimitForTest(1024);   // one 8 KB snapshot already exceeds the limit
    REQUIRE(buffer.apply({ EditOperation::reverse, { 0, 10 } }));
    REQUIRE(buffer.isModified());
    REQUIRE_FALSE(buffer.canUndo());
    REQUIRE_FALSE(buffer.undo());
    REQUIRE(buffer.isModified());
}
