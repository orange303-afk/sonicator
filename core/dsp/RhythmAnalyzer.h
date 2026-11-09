#pragma once
#include "core/scanner/FileScanner.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
namespace sonicator {
// What a short sample looks like rhythmically. These are the three answers the library needs:
// a probable one-shot, a probable rhythmic loop/fragment, or an honest "not enough evidence".
enum class RhythmKind { Undetermined, OneShot, RhythmicLoop };
enum class RhythmTempo { Unknown, Ambiguous, Determined };

// Thresholds and bounds are named here instead of being scattered through the algorithm, so the
// behaviour can be reviewed and tuned without reverse-engineering the code. The defaults are the
// values the tests and the acceptance measurements were made with.
struct RhythmOptions
{
    // Only the first N seconds are decoded. A sample longer than this is still analysed, but the
    // result describes that prefix and carries a limitation note; it is never presented as a
    // whole-file verdict.
    double maxAnalysisSeconds = 60.0;
    double minBpm = 40.0, maxBpm = 240.0;
    double minOnsetSeparationSeconds = 0.06;
    // Peak at or below this is digital silence for this analysis, matching the library's Hide
    // empty threshold (-60 dBFS).
    double silencePeak = 0.001;
    int minRhythmicOnsets = 4;
    // A tempo needs at least this many beats of context; fewer cannot distinguish a rhythm from
    // a coincidence of two hits.
    double minBeatsForTempo = 3.5;
    double minGridCoverage = 0.45;  // share of onset strength explained by the beat grid
    double minGridFill = 0.35;      // share of grid positions that carry an onset
    double minGridScore = 0.30;     // average onset strength on the grid
    double determinedGridScore = 0.40;
    // A competing grid with this share of the winner's score makes the tempo ambiguous.
    double ambiguityScoreShare = 0.80;
    // A grid with almost the same score but an unrelated ratio is ambiguous too.
    double unrelatedAmbiguityScoreShare = 0.92;
    // The ratio window around 2x and 0.5x that counts as the same tempo at half/double time.
    double halfDoubleRatioTolerance = 0.10;
    int oneShotMaxOnsets = 3;
    double oneShotMaxActiveSeconds = 4.0;
    double oneShotMinCrest = 3.0;   // peak / active RMS; a pad or texture stays below this
    double oneShotMinOnsetStrength = 0.30;
};

struct RhythmAnalysis
{
    RhythmKind kind = RhythmKind::Undetermined;
    double kindConfidence = 0.0;   // documented heuristic score 0..1, never a probability
    RhythmTempo tempo = RhythmTempo::Unknown;
    double bpm = 0.0;
    double bpmConfidence = 0.0;    // documented heuristic score 0..1, never a probability
    std::vector<double> alternatives; // plausible competing tempi, strongest first
    int onsetCount = 0;
    double durationSeconds = 0.0;  // analysed prefix
    double activeSeconds = 0.0;    // span from first to last audible frame
    juce::String explanation;      // short, deterministic, safe to store and show
    juce::String error;            // non-empty only when the audio could not be analysed
};

// Pure analysis of decoded audio. Interleaving is irrelevant: channels are handled separately and
// their spectra are averaged as powers, so anti-phase stereo is not cancelled by a mono downmix.
class RhythmAnalyzer
{
public:
    static RhythmAnalysis analyze(const juce::AudioBuffer<float>&, double sampleRate,
                                  const RhythmOptions& = {}, const CancelCheck& = {});
    // Reads only the analysed prefix through JUCE's format readers. No network, no cache files.
    static RhythmAnalysis analyzeFile(const juce::File&, const RhythmOptions& = {},
                                      const CancelCheck& = {});
};
} // namespace sonicator
