#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace sonicator {
struct AcousticDescriptors {
    double logAttackTime = 0.0;     // log10(seconds) from 10% to 90% peak power; e.g. 0.002s -> log10(0.002) ~ -2.7
    double decayRate = 0.0;         // dB/sec decay slope after peak
    double crestFactor = 0.0;       // peak / RMS (linear ratio)
    double temporalCentroid = 0.0;  // normalized 0..1 energy centroid in time
    std::array<double, 5> subBandEnergy {}; // [Sub<80Hz, Low 80-350Hz, Mid 350-2500Hz, High-Mid 2.5-7kHz, Air>7kHz] normalized sum = 1.0 (or all 0 if silence)
    double zeroCrossingRate = 0.0;  // zero crossings per second
    double spectralFlux = 0.0;      // mean L2 norm of frame spectral differences
    double pitchClarity = 0.0;      // 0..1 from autocorrelation/YIN
};

class AcousticFeatures {
public:
    static AcousticDescriptors extract(const juce::AudioBuffer<float>& buffer, double sampleRate);
};
}
