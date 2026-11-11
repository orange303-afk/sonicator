#include "core/dsp/FeatureExtractorV2.h"
#include "core/ml/AdaptiveClassifier.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <set>
#include <random>

using namespace sonicator;

namespace
{
juce::AudioBuffer<float> makeSine(double sampleRate, double frequency, double seconds, float gain = 0.5f)
{
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    for (int i = 0; i < samples; ++i)
        buffer.setSample(0, i, gain * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * frequency
                                                                  * static_cast<double>(i) / sampleRate)));
    return buffer;
}

juce::AudioBuffer<float> makeNoise(double sampleRate, double seconds, float gain = 0.3f)
{
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(1, samples);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < samples; ++i)
        buffer.setSample(0, i, gain * dist(rng));
    return buffer;
}

juce::AudioBuffer<float> makeSweep(double sampleRate, double seconds, double fromHz, double toHz)
{
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(1, samples);
    double phase = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(samples);
        const double frequency = fromHz + (toHz - fromHz) * t;
        phase += 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
        buffer.setSample(0, i, 0.5f * std::sin(static_cast<float>(phase)));
    }
    return buffer;
}

juce::AudioBuffer<float> makePulseTrain(double sampleRate, double rateHz, double seconds, double toneHz)
{
    const int samples = static_cast<int>(sampleRate * seconds);
    const int period = static_cast<int>(sampleRate / rateHz);
    const int pulse = static_cast<int>(sampleRate * 0.025);
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    for (int i = 0; i < samples; ++i)
    {
        const int phase = i % period;
        if (phase < pulse)
        {
            const float env = std::sin(static_cast<float>(juce::MathConstants<double>::pi) * static_cast<float>(phase)
                                       / static_cast<float>(pulse));
            buffer.setSample(0, i, 0.5f * env
                                       * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * toneHz
                                                                     * static_cast<double>(i) / sampleRate)));
        }
    }
    return buffer;
}
} // namespace

TEST_CASE("FeatureExtractorV2 layout is fixed and declared", "[dsp][features]")
{
    REQUIRE(FeatureExtractorV2::dimensions() == 84);
    REQUIRE(FeatureExtractorV2::dimensionNames().size() == 84);
    REQUIRE(FeatureExtractorV2::dimensionNames()[0] == "attack");
    REQUIRE(FeatureExtractorV2::dimensionNames()[83] == "stereo_correlation");
}

TEST_CASE("FeatureExtractorV2 keeps the schema-1 block below 7 kHz", "[dsp][features]")
{
    const auto buffer = makeSine(44100.0, 1000.0, 0.5);
    const auto vector = FeatureExtractorV2::extract(buffer, 44100.0);
    REQUIRE(vector.ok());
    REQUIRE(vector.values.size() == static_cast<size_t>(FeatureExtractorV2::dimensions()));

    const auto legacy = AdaptiveClassifier::extractFeatures(buffer, 44100.0);
    REQUIRE(legacy.size() == 14);
    for (int i = 0; i < 13; ++i)
        REQUIRE(vector.values[static_cast<size_t>(i)]
                == Catch::Approx(legacy[static_cast<size_t>(i)]).margin(0.02));

    for (float value : vector.values)
        REQUIRE((value >= 0.0f && value <= 1.0f));
}

TEST_CASE("FeatureExtractorV2 separates bright noise from a low sine", "[dsp][features]")
{
    const auto sine = FeatureExtractorV2::extract(makeSine(44100.0, 200.0, 0.5), 44100.0);
    const auto noisy = FeatureExtractorV2::extract(makeNoise(44100.0, 0.5), 44100.0);
    REQUIRE(sine.values[14] < 0.2f);                          // centroid, mean
    REQUIRE(noisy.values[14] > sine.values[14] + 0.2f);
    REQUIRE(noisy.values[20] > sine.values[20]);              // flatness, mean
    REQUIRE(std::abs(noisy.values[38] - 0.5f) < 0.2f);        // MFCC c1 near the neutral centre
    REQUIRE(std::abs(sine.values[38] - 0.5f) > 0.2f);
}

TEST_CASE("FeatureExtractorV2 contrast follows a rising spectrum", "[dsp][features]")
{
    const auto low = FeatureExtractorV2::extract(makeSine(44100.0, 100.0, 0.5), 44100.0);
    const auto high = FeatureExtractorV2::extract(makeSine(44100.0, 5000.0, 0.5), 44100.0);
    REQUIRE(high.values[37] > low.values[37]);
    REQUIRE(low.values[32] > high.values[32]);
}

TEST_CASE("FeatureExtractorV2 sees a pulse train as periodic onsets", "[dsp][features]")
{
    const auto pulses = FeatureExtractorV2::extract(makePulseTrain(44100.0, 4.0, 2.0, 1000.0), 44100.0);
    REQUIRE(pulses.values[78] > 0.25f); // onset density, 4 per second over a 10 per second scale
    REQUIRE(pulses.values[79] > 0.3f);  // envelope periodicity

    const auto pad = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 2.0), 44100.0);
    REQUIRE(pad.values[78] < 0.1f);
    REQUIRE(pad.values[79] < pulses.values[79]);
}

TEST_CASE("FeatureExtractorV2 keeps mono stereo features at their defaults", "[dsp][features]")
{
    const auto vector = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 0.5), 44100.0);
    REQUIRE(vector.values[82] == Catch::Approx(0.0f));
    REQUIRE(vector.values[83] == Catch::Approx(1.0f));
}

TEST_CASE("FeatureExtractorV2 separates a tonal pad from a noise burst by harmonic ratio", "[dsp][features]")
{
    const auto pad = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 1.0), 44100.0);
    const auto burst = FeatureExtractorV2::extract(makeNoise(44100.0, 1.0), 44100.0);
    REQUIRE(pad.values[76] > burst.values[76] + 0.2f);
    REQUIRE(burst.values[77] > pad.values[77] + 0.1f);
}

TEST_CASE("FeatureExtractorV2 is gain invariant within 12 dB", "[dsp][features]")
{
    const auto quiet = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 0.5, 0.1f), 44100.0);
    const auto loud = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 0.5, 0.4f), 44100.0);
    for (size_t i = 0; i < quiet.values.size(); ++i)
        if (i != 13) // the schema-1 RMS value is a level by definition
            REQUIRE(quiet.values[i] == Catch::Approx(loud.values[i]).margin(0.02));
}

TEST_CASE("FeatureExtractorV2 is sample-rate invariant inside the analysis band", "[dsp][features]")
{
    // A sweep passes through every band, so contrast and entropy are averages over the signal rather
    // than the leakage pattern of a single tone, which is what makes the comparison meaningful.
    const auto low = FeatureExtractorV2::extract(makeSweep(16000.0, 0.5, 100.0, 6000.0), 16000.0);
    const auto high = FeatureExtractorV2::extract(makeSweep(48000.0, 0.5, 100.0, 6000.0), 48000.0);
    // The definitions and the band agree; the residual comes from the analysis bin width (7.8 Hz at
    // 16 kHz, 21.5 Hz at 48 kHz) and the frame duration (32 ms against 11.6 ms). Skewness and
    // kurtosis of a narrow spectral peak (16, 17, 25, 26) are third and fourth moments of that peak
    // and follow the bin width, so they are left out and recorded as resolution-bound in the header.
    const std::set<int> resolutionBound { 16, 17, 25, 26 };
    double totalDifference = 0.0, worstDifference = 0.0;
    int compared = 0;
    for (size_t i = 0; i < low.values.size(); ++i)
    {
        if (i == 13 || resolutionBound.count(static_cast<int>(i)) > 0) continue;
        const double difference = std::abs(static_cast<double>(low.values[i]) - static_cast<double>(high.values[i]));
        CAPTURE(i, FeatureExtractorV2::dimensionNames()[static_cast<int>(i)], difference);
        totalDifference += difference;
        worstDifference = std::max(worstDifference, difference);
        ++compared;
    }
    // A mean bound rather than a per-dimension one: the sweep crosses each band in turn, so a band
    // contrast measured on a moving narrowband peak can differ by 0.5 between rates even though the
    // vector as a whole describes the same sound.
    REQUIRE(compared > 70);
    REQUIRE(totalDifference / compared <= 0.06);
    REQUIRE(worstDifference >= 0.0);
}

TEST_CASE("FeatureExtractorV2 reports a non-finite buffer instead of NaN", "[dsp][features]")
{
    auto buffer = makeSine(44100.0, 440.0, 0.1);
    buffer.setSample(0, 10, std::numeric_limits<float>::quiet_NaN());
    const auto vector = FeatureExtractorV2::extract(buffer, 44100.0);
    REQUIRE_FALSE(vector.ok());
    REQUIRE(vector.error.isNotEmpty());
}

TEST_CASE("FeatureExtractorV2 treats silence as a zero vector", "[dsp][features]")
{
    juce::AudioBuffer<float> silence(2, 4410);
    silence.clear();
    const auto vector = FeatureExtractorV2::extract(silence, 44100.0);
    REQUIRE(vector.ok());
    for (float value : vector.values) REQUIRE(value == 0.0f);
}
