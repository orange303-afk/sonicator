#pragma once
#include "core/scanner/FileScanner.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
namespace sonicator {
// Thresholds are named here so the calibration in the evaluation test is reviewable. They are
// heuristic gates, not probabilities.
struct TonalOptions
{
    double maxKeySeconds = 30.0;
    int keyFftSize = 8192;
    int keyHop = 1024;
    double minKeyFrequency = 55.0, maxKeyFrequency = 5000.0;
    double tuningMinFrequency = 60.0, tuningMaxFrequency = 3000.0;
    double minTonality = 0.30;
    double minKeyCorrelation = 0.35;
    double minTonicMargin = 0.04;
    double modeMargin = 0.03;
    double percussiveFlatness = 0.55;
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
    bool percussive = false;
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
};
} // namespace sonicator
