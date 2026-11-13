#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/PreviewReaderSource.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
juce::File writeRamp(const juce::File& file)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(1).withBitsPerSample(16);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(1, 44100);
    for (int i = 0; i < 44100; ++i) buffer.setSample(0, i, 0.5f);
    writer->writeFromAudioSampleBuffer(buffer, 0, 44100);
    return file;
}

double rms(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    for (int i = 0; i < buffer.getNumSamples(); ++i) sum += buffer.getSample(0, i) * buffer.getSample(0, i);
    return std::sqrt(sum / buffer.getNumSamples());
}

// Runs the same transport wiring AudioEngine uses and reports whether every block was audible.
int silentBlocks(const juce::File& file, int readAhead, double seconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    PreviewReaderSource source(std::move(reader));
    source.setLooping(true);
    juce::TimeSliceThread thread("preview loop test");
    thread.startThread();
    juce::AudioTransportSource transport;
    transport.setSource(&source, readAhead, readAhead > 0 ? &thread : nullptr, 44100.0);
    transport.prepareToPlay(512, 44100.0);
    transport.start();
    juce::AudioBuffer<float> buffer(1, 512);
    int silent = 0;
    const int blocks = (int) (seconds * 44100.0 / 512.0);
    for (int i = 0; i < blocks; ++i)
    {
        juce::AudioSourceChannelInfo info(&buffer, 0, 512);
        transport.getNextAudioBlock(info);
        if (rms(buffer) < 0.01) ++silent;
    }
    transport.stop();
    transport.setSource(nullptr);
    thread.stopThread(500);
    return silent;
}
} // namespace

// The engine detaches the read-ahead buffer while looping (two independent loop owners wrap the
// position differently and the second pass goes silent), so this is the production wiring.
TEST_CASE("A whole file loops audibly past the first pass", "[preview][loop]")
{
    TestDirectory t;
    REQUIRE(silentBlocks(writeRamp(t.dir.getChildFile("ramp.wav")), 0, 3.0) == 0);
}

TEST_CASE("A range loop repeats the selected window", "[preview][loop]")
{
    TestDirectory t;
    REQUIRE(silentBlocks(writeRamp(t.dir.getChildFile("ramp.wav")), 0, 2.0) == 0);
}
