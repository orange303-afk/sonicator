#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/TonalAnalyzer.h"
#include <cmath>
#include <random>
using namespace sonicator;
namespace {
constexpr double defaultRate = 44100.0;

juce::AudioBuffer<float> makeBuffer(double seconds, double sampleRate, int channels = 1)
{
    juce::AudioBuffer<float> buffer(channels, (int) std::llround(seconds * sampleRate));
    buffer.clear();
    return buffer;
}

void addSine(juce::AudioBuffer<float>& buffer, double sampleRate, double frequency,
             double startSeconds, double durationSeconds, double amplitude,
             int channel = 0, double phase = 0.0)
{
    const int start = (int) std::llround(startSeconds * sampleRate);
    const int length = (int) std::llround(durationSeconds * sampleRate);
    const int fade = (int) std::llround(0.01 * sampleRate);
    for (int i = 0; i < length && start + i < buffer.getNumSamples(); ++i)
    {
        const double t = (double) i / sampleRate;
        double envelope = 1.0;
        if (i < fade) envelope = (double) i / (double) fade;
        if (i > length - fade) envelope = (double) (length - i) / (double) fade;
        const double value = amplitude * envelope
                             * std::sin(2.0 * juce::MathConstants<double>::pi * frequency * t + phase);
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            buffer.addSample(c, start + i, (float) value);
    }
}

double midiFrequency(double midi)
{
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

juce::AudioBuffer<float> triad(int rootPitchClass, int third, double sampleRate = defaultRate,
                               double seconds = 2.0, double detuneCents = 0.0)
{
    auto buffer = makeBuffer(seconds, sampleRate);
    const double root = 60.0 + rootPitchClass;
    for (double note : { root, root + third, root + 7.0 })
    {
        const double frequency = midiFrequency(note + detuneCents / 100.0);
        addSine(buffer, sampleRate, frequency, 0.0, seconds, 0.16);
    }
    return buffer;
}

juce::AudioBuffer<float> singleNote(double midi, double sampleRate = defaultRate,
                                    double seconds = 2.0, double detuneCents = 0.0)
{
    auto buffer = makeBuffer(seconds, sampleRate);
    addSine(buffer, sampleRate, midiFrequency(midi + detuneCents / 100.0), 0.0, seconds, 0.5);
    return buffer;
}

juce::AudioBuffer<float> whiteNoise(double seconds, double sampleRate = defaultRate)
{
    auto buffer = makeBuffer(seconds, sampleRate);
    std::mt19937 generator(12345);
    std::uniform_real_distribution<double> distribution(-0.5, 0.5);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, (float) distribution(generator));
    return buffer;
}
} // namespace

TEST_CASE("A C major triad resolves C major", "[tonal][key]")
{
    const auto audio = triad(0, 4);
    const auto key = TonalAnalyzer::analyzeKey(audio, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.error.isEmpty());
    REQUIRE(key.found);
    REQUIRE(key.tonic == 0);
    REQUIRE(key.mode == "major");
}

TEST_CASE("An A minor triad resolves A minor", "[tonal][key]")
{
    const auto audio = triad(9, 3);
    const auto key = TonalAnalyzer::analyzeKey(audio, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.tonic == 9);
    REQUIRE(key.mode == "minor");
}

TEST_CASE("All twelve major and minor triads resolve", "[tonal][key]")
{
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        const auto majorAudio = triad(tonic, 4);
        const auto major = TonalAnalyzer::analyzeKey(majorAudio, defaultRate);
        INFO("major tonic " << tonic << ": " << major.explanation);
        REQUIRE(major.found);
        REQUIRE(major.tonic == tonic);
        REQUIRE(major.mode == "major");

        const auto minorAudio = triad(tonic, 3);
        const auto minor = TonalAnalyzer::analyzeKey(minorAudio, defaultRate);
        INFO("minor tonic " << tonic << ": " << minor.explanation);
        REQUIRE(minor.found);
        REQUIRE(minor.tonic == tonic);
        REQUIRE(minor.mode == "minor");
    }
}

TEST_CASE("A major scale resolves its key", "[tonal][key]")
{
    auto buffer = makeBuffer(2.0, defaultRate);
    int index = 0;
    for (int interval : { 0, 2, 4, 5, 7, 9, 11 })
        addSine(buffer, defaultRate, midiFrequency(60 + interval), 0.05 + 0.25 * index++,
                0.30, 0.35);
    const auto key = TonalAnalyzer::analyzeKey(buffer, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.tonic == 0);
    REQUIRE(key.mode == "major");
}

TEST_CASE("A single tone gives a bare tonic, not an invented mode", "[tonal][key]")
{
    const auto audio = singleNote(69); // A4
    const auto key = TonalAnalyzer::analyzeKey(audio, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.tonic == 9);
    REQUIRE(key.mode.isEmpty());
}

TEST_CASE("Silence and noise give no key", "[tonal][key]")
{
    const auto silence = makeBuffer(2.0, defaultRate);
    const auto silentKey = TonalAnalyzer::analyzeKey(silence, defaultRate);
    REQUIRE_FALSE(silentKey.found);

    const auto noise = whiteNoise(2.0);
    const auto noiseKey = TonalAnalyzer::analyzeKey(noise, defaultRate);
    INFO("explanation: " << noiseKey.explanation);
    REQUIRE_FALSE(noiseKey.found);
}

TEST_CASE("The key survives 48 and 96 kHz", "[tonal][key]")
{
    for (double rate : { 48000.0, 96000.0 })
    {
        const auto audio = triad(0, 4, rate);
        const auto key = TonalAnalyzer::analyzeKey(audio, rate);
        INFO("rate " << rate << ": " << key.explanation);
        REQUIRE(key.found);
        REQUIRE(key.tonic == 0);
        REQUIRE(key.mode == "major");
    }
}

TEST_CASE("Anti-phase stereo keeps its key", "[tonal][key]")
{
    auto mono = triad(0, 4);
    juce::AudioBuffer<float> stereo(2, mono.getNumSamples());
    stereo.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
    stereo.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
    stereo.applyGain(1, 0, mono.getNumSamples(), -1.0f);
    const auto key = TonalAnalyzer::analyzeKey(stereo, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.tonic == 0);
    REQUIRE(key.mode == "major");
}

TEST_CASE("A file shorter than the minimum gives no key", "[tonal][key]")
{
    const auto audio = triad(0, 4, defaultRate, 0.10);
    const auto key = TonalAnalyzer::analyzeKey(audio, defaultRate);
    REQUIRE_FALSE(key.found);
}
