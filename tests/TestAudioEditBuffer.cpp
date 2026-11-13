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
