#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/TonalAnalyzer.h"
#include <cmath>
#include <random>
#include <utility>
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

TEST_CASE("A tuned kick loop stays unknown", "[tonal][key][decision]")
{
    auto buffer = makeBuffer(4.0, defaultRate);
    for (double time = 0.1; time < 4.0; time += 0.5)
    {
        const int start = (int) std::llround(time * defaultRate);
        const int length = (int) std::llround(0.5 * defaultRate);
        for (int i = 0; i < length && start + i < buffer.getNumSamples(); ++i)
        {
            const double t = (double) i / defaultRate;
            const double body = 0.8 * std::exp(-t / 0.20)
                * std::sin(2.0 * juce::MathConstants<double>::pi * 80.0 * t);
            const double click = i < 80 ? 0.25 * (1.0 - (double) i / 80.0) : 0.0;
            buffer.addSample(0, start + i, (float) (body + click));
        }
    }
    const auto key = TonalAnalyzer::analyzeKey(buffer, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE_FALSE(key.found);
}

TEST_CASE("A harmonic bass note keeps its key", "[tonal][key][decision]")
{
    auto buffer = makeBuffer(2.0, defaultRate);
    const double fundamental = 440.0 * std::pow(2.0, (36.0 - 69.0) / 12.0); // C2
    addSine(buffer, defaultRate, fundamental, 0.0, 2.0, 0.30);
    addSine(buffer, defaultRate, fundamental * 2.0, 0.0, 2.0, 0.15);
    addSine(buffer, defaultRate, fundamental * 3.0, 0.0, 2.0, 0.08);
    const auto key = TonalAnalyzer::analyzeKey(buffer, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.tonic == 0);
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

TEST_CASE("A detuned triad keeps its key and reports the tuning", "[tonal][key][decision]")
{
    const auto audio = triad(0, 4, defaultRate, 2.0, 25.0);
    const auto key = TonalAnalyzer::analyzeKey(audio, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.tonic == 0);
    REQUIRE(key.mode == "major");
    REQUIRE(key.tuningCents == Catch::Approx(25.0).margin(8.0));
}

TEST_CASE("An impulse train is percussive, not a key", "[tonal][key][decision]")
{
    auto buffer = makeBuffer(4.0, defaultRate);
    for (double time = 0.1; time < 4.0; time += 0.5)
    {
        const int index = (int) std::llround(time * defaultRate);
        if (index < buffer.getNumSamples()) buffer.setSample(0, index, 0.9f);
    }
    const auto key = TonalAnalyzer::analyzeKey(buffer, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE_FALSE(key.found);
    REQUIRE(key.percussive);
}

TEST_CASE("Noise bursts are percussive, not a key", "[tonal][key][decision]")
{
    auto buffer = makeBuffer(4.0, defaultRate);
    std::mt19937 generator(999);
    std::uniform_real_distribution<double> distribution(-0.6, 0.6);
    for (double time = 0.1; time < 4.0; time += 0.25)
    {
        const int start = (int) std::llround(time * defaultRate);
        const int length = (int) std::llround(0.06 * defaultRate);
        for (int i = 0; i < length && start + i < buffer.getNumSamples(); ++i)
        {
            const double envelope = std::exp(-(double) i / (0.015 * defaultRate));
            buffer.addSample(0, start + i, (float) (distribution(generator) * envelope));
        }
    }
    const auto key = TonalAnalyzer::analyzeKey(buffer, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE_FALSE(key.found);
}

TEST_CASE("A tritone-split chord gives no key", "[tonal][key][decision]")
{
    auto buffer = makeBuffer(2.0, defaultRate);
    for (int root : { 0, 6 })
    {
        const double midiRoot = 60.0 + root;
        for (double note : { midiRoot, midiRoot + 4.0, midiRoot + 7.0 })
            addSine(buffer, defaultRate, midiFrequency(note), 0.0, 2.0, 0.10);
    }
    const auto key = TonalAnalyzer::analyzeKey(buffer, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE_FALSE(key.found);
}

TEST_CASE("Competing modes at one tonic give a bare tonic", "[tonal][key][decision]")
{
    auto buffer = makeBuffer(2.0, defaultRate);
    for (double note : { 60.0, 64.0, 67.0 })
        addSine(buffer, defaultRate, midiFrequency(note), 0.0, 1.0, 0.18);
    for (double note : { 60.0, 63.0, 67.0 })
        addSine(buffer, defaultRate, midiFrequency(note), 1.0, 1.0, 0.18);
    const auto key = TonalAnalyzer::analyzeKey(buffer, defaultRate);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.tonic == 0);
    REQUIRE(key.mode.isEmpty());
}

TEST_CASE("A capped key analysis records the prefix", "[tonal][key]")
{
    TonalOptions options;
    options.maxKeySeconds = 0.5;
    const auto key = TonalAnalyzer::analyzeKey(triad(0, 4), defaultRate, options);
    INFO("explanation: " << key.explanation);
    REQUIRE(key.found);
    REQUIRE(key.explanation.contains("first"));
}

TEST_CASE("A found key has a bounded confidence and an explanation", "[tonal][key][decision]")
{
    const auto audio = triad(0, 4);
    const auto key = TonalAnalyzer::analyzeKey(audio, defaultRate);
    REQUIRE(key.found);
    REQUIRE(std::isfinite(key.confidence));
    REQUIRE(key.confidence >= 0.0);
    REQUIRE(key.confidence <= 1.0);
    REQUIRE(key.explanation.containsIgnoreCase("Krumhansl"));
    REQUIRE(key.explanation.containsIgnoreCase("margin"));
}

TEST_CASE("A440 resolves to MIDI 69", "[tonal][pitch]")
{
    const auto audio = singleNote(69);
    const auto pitch = TonalAnalyzer::analyzePitch(audio, defaultRate);
    INFO("explanation: " << pitch.explanation);
    REQUIRE(pitch.error.isEmpty());
    REQUIRE(pitch.found);
    REQUIRE(pitch.midi == Catch::Approx(69.0).margin(0.02));
    REQUIRE(pitch.cents == Catch::Approx(0.0).margin(2.0));
}

TEST_CASE("A detuned note reports its cents", "[tonal][pitch]")
{
    const auto audio = singleNote(69, defaultRate, 2.0, 25.0);
    const auto pitch = TonalAnalyzer::analyzePitch(audio, defaultRate);
    INFO("explanation: " << pitch.explanation);
    REQUIRE(pitch.found);
    REQUIRE(pitch.midi == Catch::Approx(69.25).margin(0.03));
    REQUIRE(pitch.cents == Catch::Approx(25.0).margin(3.0));
}

TEST_CASE("Low notes resolve", "[tonal][pitch]")
{
    for (auto [midi, label] : { std::pair<double, const char*> { 48.0, "C3" },
                                std::pair<double, const char*> { 40.0, "E2" } })
    {
        const auto audio = singleNote(midi);
        const auto pitch = TonalAnalyzer::analyzePitch(audio, defaultRate);
        INFO(label << ": " << pitch.explanation);
        REQUIRE(pitch.found);
        REQUIRE(pitch.midi == Catch::Approx(midi).margin(0.05));
    }
}

TEST_CASE("Pitch survives 48 and 96 kHz", "[tonal][pitch]")
{
    for (double rate : { 48000.0, 96000.0 })
    {
        const auto audio = singleNote(69, rate);
        const auto pitch = TonalAnalyzer::analyzePitch(audio, rate);
        INFO("rate " << rate << ": " << pitch.explanation);
        REQUIRE(pitch.found);
        REQUIRE(pitch.midi == Catch::Approx(69.0).margin(0.05));
    }
}

TEST_CASE("Silence and noise give no pitch", "[tonal][pitch]")
{
    const auto silence = makeBuffer(1.0, defaultRate);
    REQUIRE_FALSE(TonalAnalyzer::analyzePitch(silence, defaultRate).found);

    const auto noise = whiteNoise(1.0);
    INFO("explanation: " << TonalAnalyzer::analyzePitch(noise, defaultRate).explanation);
    REQUIRE_FALSE(TonalAnalyzer::analyzePitch(noise, defaultRate).found);
}

TEST_CASE("A glide gives no pitch", "[tonal][pitch]")
{
    auto buffer = makeBuffer(2.0, defaultRate);
    double phase = 0.0;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const double t = (double) i / defaultRate;
        const double frequency = 220.0 + (110.0 - 220.0) * (t / 2.0);
        phase += 2.0 * juce::MathConstants<double>::pi * frequency / defaultRate;
        buffer.setSample(0, i, (float) (0.5 * std::sin(phase)));
    }
    const auto pitch = TonalAnalyzer::analyzePitch(buffer, defaultRate);
    INFO("explanation: " << pitch.explanation);
    REQUIRE_FALSE(pitch.found);
}

TEST_CASE("A noise burst gives no pitch", "[tonal][pitch]")
{
    auto buffer = makeBuffer(0.5, defaultRate);
    std::mt19937 generator(77);
    std::uniform_real_distribution<double> distribution(-0.7, 0.7);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const double envelope = std::exp(-(double) i / (0.05 * defaultRate));
        buffer.setSample(0, i, (float) (distribution(generator) * envelope));
    }
    const auto pitch = TonalAnalyzer::analyzePitch(buffer, defaultRate);
    INFO("explanation: " << pitch.explanation);
    REQUIRE_FALSE(pitch.found);
}

TEST_CASE("A chord gives no single pitch", "[tonal][pitch]")
{
    const auto audio = triad(0, 4);
    const auto pitch = TonalAnalyzer::analyzePitch(audio, defaultRate);
    INFO("explanation: " << pitch.explanation);
    REQUIRE_FALSE(pitch.found);
}

TEST_CASE("Anti-phase stereo keeps its pitch", "[tonal][pitch]")
{
    const auto mono = singleNote(69);
    juce::AudioBuffer<float> stereo(2, mono.getNumSamples());
    stereo.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
    stereo.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
    stereo.applyGain(1, 0, mono.getNumSamples(), -1.0f);
    const auto pitch = TonalAnalyzer::analyzePitch(stereo, defaultRate);
    INFO("explanation: " << pitch.explanation);
    REQUIRE(pitch.found);
    REQUIRE(pitch.midi == Catch::Approx(69.0).margin(0.02));
}

TEST_CASE("A found pitch has a bounded confidence", "[tonal][pitch]")
{
    const auto audio = singleNote(69);
    const auto pitch = TonalAnalyzer::analyzePitch(audio, defaultRate);
    REQUIRE(pitch.found);
    REQUIRE(std::isfinite(pitch.confidence));
    REQUIRE(pitch.confidence >= 0.0);
    REQUIRE(pitch.confidence <= 1.0);
}

TEST_CASE("A capped pitch analysis records the prefix", "[tonal][pitch]")
{
    TonalOptions options;
    options.maxPitchSeconds = 0.5;
    const auto pitch = TonalAnalyzer::analyzePitch(singleNote(69), defaultRate, options);
    INFO("explanation: " << pitch.explanation);
    REQUIRE(pitch.found);
    REQUIRE(pitch.explanation.contains("first"));
}


