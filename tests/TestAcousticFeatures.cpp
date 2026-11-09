#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/AcousticFeatures.h"
#include <cmath>

using namespace sonicator;

TEST_CASE("AcousticFeatures extracts instant click descriptors", "[dsp][acoustic]")
{
    const double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(sampleRate * 0.5); // 0.5s
    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();

    // Instant click: sharp impulse at sample 0, rapid decay
    buffer.setSample(0, 0, 1.0f);
    for (int i = 1; i < 50; ++i)
    {
        buffer.setSample(0, i, std::exp(-0.2f * static_cast<float>(i)));
    }

    auto desc = AcousticFeatures::extract(buffer, sampleRate);

    // Fast attack duration <= 0.005s (i.e. logAttackTime <= log10(0.005) ~ -2.301)
    double attackDuration = std::pow(10.0, desc.logAttackTime);
    REQUIRE(attackDuration <= 0.005);
    REQUIRE(desc.logAttackTime <= std::log10(0.005));

    // High crest factor
    REQUIRE(desc.crestFactor > 5.0);

    // Energy should be concentrated near the start
    REQUIRE(desc.temporalCentroid < 0.1);

    // Check finite numbers
    REQUIRE(std::isfinite(desc.logAttackTime));
    REQUIRE(std::isfinite(desc.decayRate));
    REQUIRE(std::isfinite(desc.crestFactor));
    REQUIRE(std::isfinite(desc.temporalCentroid));
    REQUIRE(std::isfinite(desc.zeroCrossingRate));
    REQUIRE(std::isfinite(desc.spectralFlux));
    REQUIRE(std::isfinite(desc.pitchClarity));
}

TEST_CASE("AcousticFeatures extracts slow ramp pad descriptors", "[dsp][acoustic]")
{
    const double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(sampleRate * 0.5); // 0.5s
    juce::AudioBuffer<float> buffer(1, numSamples);

    // Envelope slowly rising linearly over 0.5s with a 440 Hz carrier
    for (int i = 0; i < numSamples; ++i)
    {
        float envelope = static_cast<float>(i) / static_cast<float>(numSamples - 1);
        float carrier = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * static_cast<float>(i) / static_cast<float>(sampleRate));
        buffer.setSample(0, i, envelope * carrier);
    }

    auto desc = AcousticFeatures::extract(buffer, sampleRate);

    // Slow attack duration > 0.1s => logAttackTime > -1.0
    REQUIRE(desc.logAttackTime > -1.0);

    // Moderate crest factor (< 3.0)
    REQUIRE(desc.crestFactor < 3.0);

    // Temporal centroid should be past the midpoint due to ramp
    REQUIRE(desc.temporalCentroid > 0.5);
}

TEST_CASE("AcousticFeatures extracts 50 Hz pure sine wave descriptors", "[dsp][acoustic]")
{
    const double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(sampleRate * 1.0); // 1.0s
    juce::AudioBuffer<float> buffer(1, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = std::sin(2.0f * juce::MathConstants<float>::pi * 50.0f * static_cast<float>(i) / static_cast<float>(sampleRate));
        buffer.setSample(0, i, sample);
    }

    auto desc = AcousticFeatures::extract(buffer, sampleRate);

    // 50 Hz should dominate sub-band [0, 80 Hz)
    REQUIRE(desc.subBandEnergy[0] > 0.80);

    // Pitch clarity should be very high for a pure sine wave
    REQUIRE(desc.pitchClarity > 0.80);

    // Zero-crossing rate should be ~100 crossings/sec (50 Hz * 2)
    REQUIRE(desc.zeroCrossingRate >= 95.0);
    REQUIRE(desc.zeroCrossingRate <= 105.0);

    // Sum of sub-band energy normalized to 1.0
    double sumEnergy = 0.0;
    for (double e : desc.subBandEnergy)
        sumEnergy += e;
    REQUIRE(sumEnergy == Catch::Approx(1.0).margin(1e-4));
}

TEST_CASE("AcousticFeatures extracts white noise burst descriptors", "[dsp][acoustic]")
{
    const double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(sampleRate * 1.0); // 1.0s
    juce::AudioBuffer<float> buffer(1, numSamples);

    juce::Random rng(12345);
    for (int i = 0; i < numSamples; ++i)
    {
        buffer.setSample(0, i, rng.nextFloat() * 2.0f - 1.0f);
    }

    auto desc = AcousticFeatures::extract(buffer, sampleRate);

    // White noise should have low pitch clarity (< 0.30)
    REQUIRE(desc.pitchClarity < 0.30);

    // Energy distributed across mid / high-mid / air
    double midHighAirEnergy = desc.subBandEnergy[2] + desc.subBandEnergy[3] + desc.subBandEnergy[4];
    REQUIRE(midHighAirEnergy > 0.70);

    // Sub-band energy (< 80 Hz) should be very low for flat white noise (80 / 22050 < 1%)
    REQUIRE(desc.subBandEnergy[0] < 0.10);

    // Zero-crossing rate for white noise is high (~sampleRate * 0.5)
    REQUIRE(desc.zeroCrossingRate > 10000.0);

    // Sum of sub-band energy normalized to 1.0
    double sumEnergy = 0.0;
    for (double e : desc.subBandEnergy)
        sumEnergy += e;
    REQUIRE(sumEnergy == Catch::Approx(1.0).margin(1e-4));
}

TEST_CASE("AcousticFeatures handles zero and silent buffers safely", "[dsp][acoustic]")
{
    const double sampleRate = 44100.0;

    SECTION("Empty buffer")
    {
        juce::AudioBuffer<float> emptyBuffer(0, 0);
        auto desc = AcousticFeatures::extract(emptyBuffer, sampleRate);
        REQUIRE(desc.crestFactor == 0.0);
        REQUIRE(desc.logAttackTime == 0.0);
        REQUIRE(desc.decayRate == 0.0);
        REQUIRE(desc.temporalCentroid == 0.0);
        REQUIRE(desc.zeroCrossingRate == 0.0);
        REQUIRE(desc.spectralFlux == 0.0);
        REQUIRE(desc.pitchClarity == 0.0);
        for (double e : desc.subBandEnergy)
            REQUIRE(e == 0.0);
    }

    SECTION("Silent buffer of zeros")
    {
        juce::AudioBuffer<float> silentBuffer(2, 4410);
        silentBuffer.clear();
        auto desc = AcousticFeatures::extract(silentBuffer, sampleRate);
        REQUIRE(desc.crestFactor == 0.0);
        REQUIRE(desc.pitchClarity == 0.0);
        for (double e : desc.subBandEnergy)
            REQUIRE(e == 0.0);
    }

    SECTION("Buffer below threshold (< 1e-5)")
    {
        juce::AudioBuffer<float> lowBuffer(1, 4410);
        for (int i = 0; i < 4410; ++i)
            lowBuffer.setSample(0, i, 1e-6f);
        auto desc = AcousticFeatures::extract(lowBuffer, sampleRate);
        REQUIRE(desc.crestFactor == 0.0);
        for (double e : desc.subBandEnergy)
            REQUIRE(e == 0.0);
    }

    SECTION("Invalid sample rate")
    {
        juce::AudioBuffer<float> buffer(1, 100);
        buffer.setSample(0, 0, 1.0f);
        auto desc = AcousticFeatures::extract(buffer, 0.0);
        REQUIRE(desc.crestFactor == 0.0);
        REQUIRE(desc.pitchClarity == 0.0);
    }

    SECTION("Non-finite sample values (NaN / Inf)")
    {
        juce::AudioBuffer<float> buffer(1, 1000);
        buffer.clear();
        buffer.setSample(0, 10, std::numeric_limits<float>::quiet_NaN());
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        REQUIRE(desc.crestFactor == 0.0);
        REQUIRE(desc.pitchClarity == 0.0);

        buffer.setSample(0, 10, std::numeric_limits<float>::infinity());
        desc = AcousticFeatures::extract(buffer, sampleRate);
        REQUIRE(desc.crestFactor == 0.0);
    }
}

TEST_CASE("AcousticFeatures extracts decay rate accurately", "[dsp][acoustic]")
{
    const double sampleRate = 44100.0;
    const int numSamples = static_cast<int>(sampleRate * 0.5); // 0.5s
    juce::AudioBuffer<float> buffer(1, numSamples);

    // Peak at t=0, decays by 20 dB (amplitude 0.10) in 0.1s
    // 20 dB / 0.1s = 200 dB/s
    // exp(-alpha * 0.1) = 0.10 => alpha = ln(10) / 0.1 = 23.026
    const float alpha = static_cast<float>(std::log(10.0) / 0.1);
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float env = std::exp(-alpha * t);
        float carrier = std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * t);
        buffer.setSample(0, i, env * carrier);
    }

    auto desc = AcousticFeatures::extract(buffer, sampleRate);

    // Expected decay rate ~200 dB/s (tolerance ±25 dB/s due to frame hop granularity)
    REQUIRE(desc.decayRate >= 170.0);
    REQUIRE(desc.decayRate <= 230.0);
}

TEST_CASE("AcousticFeatures preserves stereo anti-phase energy and 96kHz sample rate", "[dsp][acoustic]")
{
    const double sampleRate = 96000.0;
    const int numSamples = static_cast<int>(sampleRate * 0.5);
    juce::AudioBuffer<float> buffer(2, numSamples);

    // Left channel: 50 Hz sine; Right channel: inverted 50 Hz sine
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = std::sin(2.0f * juce::MathConstants<float>::pi * 50.0f * static_cast<float>(i) / static_cast<float>(sampleRate));
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, -sample);
    }

    auto desc = AcousticFeatures::extract(buffer, sampleRate);

    // Anti-phase energy should still be captured in Sub band without vanishing
    REQUIRE(desc.subBandEnergy[0] > 0.80);
    double sumEnergy = 0.0;
    for (double e : desc.subBandEnergy)
        sumEnergy += e;
    REQUIRE(sumEnergy == Catch::Approx(1.0).margin(1e-4));

    // Anti-phase stereo should not cancel out temporal centroid, envelope, or pitch clarity
    REQUIRE(desc.pitchClarity > 0.80);
    REQUIRE(desc.temporalCentroid > 0.40);
    REQUIRE(desc.temporalCentroid < 0.60);
    REQUIRE(desc.crestFactor > 0.0);
}
