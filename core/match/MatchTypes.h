#pragma once
#include "MatchPreferences.h"
#include <cstdint>

namespace sonicator
{
// Metadata a host catalog provides. Only "known" is safe to transform from; stale, conflicting
// and unknown values are reported as such instead of being guessed from a duration or caption.
enum class MetadataState { unknown, known, stale, conflicting, notApplicable };

// Only an explicit loop is tempo-matched. An unlabelled file is never stretched on the chance
// that it looks like one; a one-shot is explicitly not applicable.
enum class MatchStructure { unknown, loop, rexLoop, oneShot, unsupported };

struct MatchSource
{
    juce::String identity; // catalog path and fingerprint, opaque to the policy
    int64_t startFrame = 0;
    int64_t sourceFrames = 0;
    double sampleRate = 0.0;
    int channels = 0;
    MatchStructure structure = MatchStructure::unknown;
    double bpm = 0.0;
    MetadataState bpmState = MetadataState::unknown;
    int keyTonic = -1; // 0 = C .. 11 = B
    juce::String keyMode; // "major", "minor" or empty when only the tonic is known
    MetadataState keyState = MetadataState::unknown;
    bool hasPitch = false;
    int pitchMidi = -1;
    double pitchCents = 0.0;
    MetadataState pitchState = MetadataState::unknown;
    int rootNote = -1; // sampler root note, never a musical key
    MetadataState rootState = MetadataState::unknown;
    int64_t metadataRevision = 0;
};

enum class MatchAction { bypass, transform, blocked };

enum class MatchBlockReason
{
    none,
    unsupportedSource,
    hostTempoUnavailable,
    sourceBpmUnknown,
    bpmNotApplicable,
    bpmStructureUnknown,
    sourcePitchUnknown,
    modeMismatch,
    ratioOutOfRange,
    invalidLength
};

// A fully decided request. The renderer never re-derives a ratio or a semitone count: the plan is
// the single place that decides, so preview, waveform and drag cannot disagree.
struct MatchPlan
{
    MatchAction action = MatchAction::bypass;
    MatchBlockReason reason = MatchBlockReason::none;
    double timeRatio = 1.0;
    double semitones = 0.0;
    int64_t targetFrames = 0;
    bool tempoApplied = false;
    bool pitchApplied = false;
    double sourceBpm = 0.0;
    double targetBpm = 0.0;
    int targetTonic = -1;
    TargetMode targetMode = TargetMode::minor;
};

inline juce::String matchBlockReasonText (MatchBlockReason reason)
{
    switch (reason)
    {
        case MatchBlockReason::none: return {};
        case MatchBlockReason::unsupportedSource: return "Unsupported source for Match";
        case MatchBlockReason::hostTempoUnavailable: return "Host tempo unavailable";
        case MatchBlockReason::sourceBpmUnknown: return "Source BPM unknown";
        case MatchBlockReason::bpmNotApplicable: return "BPM: not applicable to one-shot";
        case MatchBlockReason::bpmStructureUnknown: return "BPM: structure unknown";
        case MatchBlockReason::sourcePitchUnknown: return "Source pitch/key unknown";
        case MatchBlockReason::modeMismatch: return "Mode mismatch; cannot match scale";
        case MatchBlockReason::ratioOutOfRange: return "Tempo ratio outside 0.5–2.0";
        case MatchBlockReason::invalidLength: return "Source length is not usable";
    }
    return "Match unavailable";
}
} // namespace sonicator
