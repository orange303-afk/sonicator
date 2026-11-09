#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/RhythmAnalyzer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <iostream>
using namespace sonicator;
namespace {
constexpr double defaultRate = 44100.0;

juce::AudioBuffer<float> makeBuffer(double seconds, double sampleRate, int channels = 1)
{
    juce::AudioBuffer<float> buffer(channels, (int) std::llround(seconds * sampleRate));
    buffer.clear();
    return buffer;
}

void addClick(juce::AudioBuffer<float>& buffer, double sampleRate, double time,
              double amplitude = 0.8, double decay = 0.03, double frequency = 1200.0)
{
    const int start = (int) std::llround(time * sampleRate);
    const int length = (int) std::llround(std::min(2.5, decay * 8.0) * sampleRate);
    for (int i = 0; i < length && start + i < buffer.getNumSamples(); ++i)
    {
        const double t = (double) i / sampleRate;
        const double value = amplitude * std::exp(-t / decay)
                             * std::sin(2.0 * juce::MathConstants<double>::pi * frequency * t);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addSample(channel, start + i, (float) value);
    }
}

juce::AudioBuffer<float> clickTrack(double bpm, double seconds, double sampleRate,
                                    double offset = 0.05, double amplitude = 0.8)
{
    auto buffer = makeBuffer(seconds, sampleRate);
    const double interval = 60.0 / bpm;
    for (double time = offset; time < seconds; time += interval)
        addClick(buffer, sampleRate, time, amplitude);
    return buffer;
}

juce::File writeWav(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr) return {};
    auto* writer = wav.createWriterFor(stream.get(), sampleRate,
                                      (unsigned) buffer.getNumChannels(), 16, {}, 0);
    if (writer == nullptr) return {};
    stream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    delete writer;
    return file;
}

juce::AudioBuffer<float> tone(double seconds, double sampleRate, double frequency,
                              double amplitude, double fadeIn = 0.0)
{
    auto buffer = makeBuffer(seconds, sampleRate);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const double t = (double) i / sampleRate;
        double envelope = 1.0;
        if (fadeIn > 0 && t < fadeIn) envelope = t / fadeIn;
        const double value = amplitude * envelope
                             * std::sin(2.0 * juce::MathConstants<double>::pi * frequency * t);
        buffer.setSample(0, i, (float) value);
    }
    return buffer;
}
} // namespace

TEST_CASE("Silence is undetermined, not a one-shot", "[rhythm]")
{
    auto buffer = makeBuffer(2.0, defaultRate);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.kind == RhythmKind::Undetermined);
    REQUIRE(result.tempo == RhythmTempo::Unknown);
    REQUIRE_FALSE(result.bpm > 0);
}

TEST_CASE("A sustained tone is neither a one-shot nor a loop", "[rhythm]")
{
    const auto buffer = tone(2.0, defaultRate, 220.0, 0.3, 0.25);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::Undetermined);
    REQUIRE(result.tempo == RhythmTempo::Unknown);
}

TEST_CASE("A single hit is a one-shot", "[rhythm]")
{
    auto buffer = makeBuffer(2.0, defaultRate);
    addClick(buffer, defaultRate, 0.2);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::OneShot);
    REQUIRE(result.tempo == RhythmTempo::Unknown);
    REQUIRE(result.kindConfidence > 0.5);
}

TEST_CASE("A hit with a long reverb tail stays a one-shot", "[rhythm]")
{
    auto buffer = makeBuffer(2.6, defaultRate);
    addClick(buffer, defaultRate, 0.1, 0.8, 0.35);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    INFO("explanation: " << result.explanation << " onsets=" << result.onsetCount
                         << " active=" << result.activeSeconds);
    REQUIRE(result.kind == RhythmKind::OneShot);
}

TEST_CASE("A four-on-the-floor loop yields its tempo", "[rhythm]")
{
    const auto buffer = clickTrack(120.0, 5.0, defaultRate);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    REQUIRE(result.tempo == RhythmTempo::Determined);
    REQUIRE(result.bpm == Catch::Approx(120.0).epsilon(0.01));
    REQUIRE(result.bpmConfidence > 0.4);
}

TEST_CASE("Tempi across the working range are detected", "[rhythm]")
{
    for (double bpm : { 90.0, 128.0, 140.0, 174.0 })
    {
        INFO("bpm " << bpm);
        const auto buffer = clickTrack(bpm, 6.0, defaultRate);
        const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
        REQUIRE(result.kind == RhythmKind::RhythmicLoop);
        REQUIRE(result.tempo == RhythmTempo::Determined);
        REQUIRE(result.bpm == Catch::Approx(bpm).epsilon(0.01));
    }
}

TEST_CASE("An accented bar does not halve the tempo", "[rhythm]")
{
    auto buffer = makeBuffer(6.0, defaultRate);
    const double interval = 60.0 / 120.0;
    int beat = 0;
    for (double time = 0.05; time < 6.0; time += interval, ++beat)
        addClick(buffer, defaultRate, time, beat % 4 == 0 ? 0.9 : 0.35);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    REQUIRE(result.tempo == RhythmTempo::Determined);
    REQUIRE(result.bpm == Catch::Approx(120.0).epsilon(0.01));
}

TEST_CASE("Eighth-note hats report the slower of an out-of-range double", "[rhythm]")
{
    auto buffer = makeBuffer(5.0, defaultRate);
    const double interval = 60.0 / 130.0 / 2.0;
    for (double time = 0.05; time < 5.0; time += interval)
        addClick(buffer, defaultRate, time, 0.6);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    REQUIRE(result.bpm == Catch::Approx(130.0).epsilon(0.01));
}

TEST_CASE("Anti-phase stereo keeps its rhythm", "[rhythm]")
{
    const auto mono = clickTrack(120.0, 5.0, defaultRate);
    juce::AudioBuffer<float> stereo(2, mono.getNumSamples());
    stereo.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
    stereo.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
    stereo.applyGain(1, 0, mono.getNumSamples(), -1.0f);
    const auto result = RhythmAnalyzer::analyze(stereo, defaultRate);
    REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    REQUIRE(result.bpm == Catch::Approx(120.0).epsilon(0.01));
}

TEST_CASE("Leading silence does not break the tempo", "[rhythm]")
{
    const auto buffer = clickTrack(100.0, 6.0, defaultRate, 1.5);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    REQUIRE(result.bpm == Catch::Approx(100.0).epsilon(0.01));
}

TEST_CASE("Quiet material is still analysed", "[rhythm]")
{
    const auto buffer = clickTrack(120.0, 5.0, defaultRate, 0.05, 0.02);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    REQUIRE(result.bpm == Catch::Approx(120.0).epsilon(0.01));
}

TEST_CASE("Two isolated hits are not a loop", "[rhythm]")
{
    auto buffer = makeBuffer(1.2, defaultRate);
    addClick(buffer, defaultRate, 0.1);
    addClick(buffer, defaultRate, 0.6);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind != RhythmKind::RhythmicLoop);
    REQUIRE(result.tempo == RhythmTempo::Unknown);
}

TEST_CASE("A very short file is undetermined", "[rhythm]")
{
    auto buffer = makeBuffer(0.05, defaultRate);
    addClick(buffer, defaultRate, 0.0);
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind == RhythmKind::Undetermined);
    REQUIRE(result.tempo == RhythmTempo::Unknown);
}

TEST_CASE("White noise is not a loop", "[rhythm]")
{
    auto buffer = makeBuffer(3.0, defaultRate);
    uint32_t state = 12345u;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        state = state * 1664525u + 1013904223u;
        const float value = ((float) (state >> 8) / 8388608.0f - 1.0f) * 0.2f;
        buffer.setSample(0, i, value);
    }
    const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
    REQUIRE(result.kind != RhythmKind::RhythmicLoop);
    REQUIRE(result.tempo == RhythmTempo::Unknown);
}

TEST_CASE("The same loop at different rates gives the same tempo", "[rhythm]")
{
    for (double rate : { 22050.0, 48000.0 })
    {
        INFO("rate " << rate);
        const auto buffer = clickTrack(128.0, 5.0, rate);
        const auto result = RhythmAnalyzer::analyze(buffer, rate);
        REQUIRE(result.kind == RhythmKind::RhythmicLoop);
        REQUIRE(result.bpm == Catch::Approx(128.0).epsilon(0.01));
    }
}

TEST_CASE("A loop is read from a WAV file", "[rhythm]")
{
    const auto buffer = clickTrack(120.0, 4.0, defaultRate);
    const auto file = writeWav(buffer, defaultRate);
    REQUIRE(file != juce::File());
    const auto result = RhythmAnalyzer::analyzeFile(file);
    file.deleteFile();
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    REQUIRE(result.bpm == Catch::Approx(120.0).epsilon(0.01));
}

TEST_CASE("A non-audio file reports an error, not a tempo", "[rhythm]")
{
    const auto file = juce::File::createTempFile(".wav");
    file.replaceWithText("this is not audio");
    const auto result = RhythmAnalyzer::analyzeFile(file);
    file.deleteFile();
    REQUIRE(result.error.isNotEmpty());
    REQUIRE_FALSE(result.bpm > 0);
}

TEST_CASE("Rhythm analysis timing on this machine", "[.][rhythm][bench]")
{
    for (double seconds : { 1.0, 5.0, 30.0, 60.0 })
    {
        const auto buffer = clickTrack(128.0, seconds, defaultRate);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto result = RhythmAnalyzer::analyze(buffer, defaultRate);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
        std::cout << "audio_seconds=" << seconds << " analysis_ms=" << elapsed
                  << " bpm=" << result.bpm << " kind=" << (int) result.kind
                  << " onsets=" << result.onsetCount << std::endl;
        if (seconds >= 5.0) REQUIRE(result.kind == RhythmKind::RhythmicLoop);
    }
}

TEST_CASE("The file reader honours the decoded-memory cap", "[rhythm]")
{
    const auto buffer = clickTrack(120.0, 4.0, defaultRate);
    const auto file = writeWav(buffer, defaultRate);
    REQUIRE(file != juce::File());
    RhythmOptions options;
    options.maxDecodedBytes = (int64_t) (defaultRate * 4.0 * 2.0); // two mono seconds
    const auto result = RhythmAnalyzer::analyzeFile(file, options);
    file.deleteFile();
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.durationSeconds == Catch::Approx(2.0).margin(0.01));
    REQUIRE(result.explanation.contains("first 2 s analysed"));
}
