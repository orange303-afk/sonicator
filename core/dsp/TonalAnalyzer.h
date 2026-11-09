#pragma once
#include "core/scanner/FileScanner.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
namespace sonicator {
// Thresholds are named here so the calibration in the evaluation test is reviewable. They are
// heuristic gates, not probabilities.
struct TonalOptions
{
    double maxKeySeconds = 30.0;
    int keyFftSize = 8192;
    int keyHop = 1024;
    double minKeyFrequency = 40.0, maxKeyFrequency = 5000.0;
    double tuningMinFrequency = 70.0, tuningMaxFrequency = 1200.0;
    double minTonality = 0.30;
    // A tonal loop keeps the same pitch classes over time; a drum loop's chroma jumps between
    // resonances. Low consistency is a percussion signal even when each hit is pitched.
    double minChromaConsistency = 0.35;
    double minLowHarmonicSupport = 0.15;
    double minDutyCycle = 0.70;
    double minKeyCorrelation = 0.37;
    double minTonicMargin = 0.06;
    double modeMargin = 0.06;
    double percussiveFlatness = 0.55;
    double peakThreshold = 0.01; // peak power relative to the frame maximum
    int medianFrames = 4;        // chroma median-filter radius in frames
    // Low-frequency tonic prior. The bass note is a stronger cue for the tonic than the full mix,
    // which often overweights the fifth.
    double bassWeight = 0.10;
    // A relative major/minor pair shares a scale; the tonic is genuinely disputed unless one
    // profile is clearly stronger. Below this margin the answer is "unknown", per the tonal rule.
    double relativeMargin = 0.10;
    // Per-frame spectral whitening: peak magnitude relative to the frame maximum, compressed by
    // this power. 1 = linear, 0.5 = square root. Each frame is L2-normalized before summing so a
    // loud section cannot outvote the rest of the loop.
    double magnitudePower = 0.5;
    bool normalizeFrames = true;

    // Pitch: YIN over the most stable segment of a one-shot.
    double maxPitchSeconds = 5.0;
    double minPitchFrequency = 50.0, maxPitchFrequency = 2000.0;
    double yinThreshold = 0.15;
    double minPitchClarity = 0.5;
    int pitchHop = 256;
    int minStableFrames = 5;
    double maxRunCents = 50.0;
    double maxStableDeviationCents = 30.0;
    double minHarmonicity = 0.55;
    double minFundamentalShare = 0.05;
    double minActiveDbfs = -45.0;
    double attackSkipSeconds = 0.02;
};

struct PitchAnalysis
{
    bool found = false;
    double midi = 0.0;       // exact fractional MIDI
    double cents = 0.0;      // (midi - round(midi)) * 100
    double clarity = 0.0, stabilityCents = 0.0, harmonicity = 0.0;
    double confidence = 0.0;
    int stableFrames = 0;
    juce::String explanation;
    juce::String error;
};

struct KeyAnalysis
{
    bool found = false;
    int tonic = -1;            // 0 = C .. 11 = B
    juce::String mode;         // "major", "minor" or empty for a confident tonic without a mode
    double confidence = 0.0;   // documented heuristic 0..1, never a probability
    double bestCorrelation = 0.0, runnerUpCorrelation = 0.0;
    int runnerUpTonic = -1;
    juce::String runnerUpMode;
    double tuningCents = 0.0;
    double tonality = 0.0;
    double chromaConsistency = 0.0;
    double rmsVariation = 0.0;
    double onsetDensity = 0.0;
    double dutyCycle = 0.0;
    double lowHarmonicSupport = 0.0;
    bool percussive = false;
    // Diagnostics for the inspector and the evaluation report. Normalized, C at index 0.
    std::array<double, 12> chroma {};
    std::array<double, 12> bassChroma {};
    juce::String explanation;
    juce::String error;
};

// Pure analysis of already decoded audio. Channels are combined as powers, so anti-phase stereo
// is not cancelled. Nothing here touches the audio callback or the database.
class TonalAnalyzer
{
public:
    static KeyAnalysis analyzeKey(const juce::AudioBuffer<float>&, double sampleRate,
                                  const TonalOptions& = {}, const CancelCheck& = {});
    static PitchAnalysis analyzePitch(const juce::AudioBuffer<float>&, double sampleRate,
                                      const TonalOptions& = {}, const CancelCheck& = {});
};
} // namespace sonicator
