#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <vector>
namespace sonicator {
// Fields that Phase 2 derives from metadata and DSP. They are deliberately separate from
// SampleRecord: measured Phase 1 descriptors and user tags must not be rewritten by them.
enum class SmartField { Type, Structure, Bpm, Key, Pitch, RootNote };
inline constexpr int smartFieldCount = 6;
juce::String smartFieldKey(SmartField);
std::optional<SmartField> smartFieldFromKey(const juce::String&);

// Versions are independent on purpose:
// - extractors: bump when a metadata parser or its interpretation changes; stored candidates
//   and extraction records carry their own version, so provenance survives a re-run.
// - resolver: bump when selection rules change; results can then be recomputed from the stored
//   candidates without reading any audio again.
// - DSP: bump when the DSP stage changes (1 = rhythm structure and BPM).
inline constexpr int smartExtractorsVersion = 2;
inline constexpr int smartResolverVersion = 2;
inline constexpr int smartDspVersion = 2; // 1 = rhythm; 2 = rhythm + key + one-shot pitch

// Invalid means "the field was present but its value is not usable"; Unsupported means the
// container/version is not handled. Both stay visible for provenance, but never resolve a value.
enum class CandidateStatus { Valid, DefaultValue, Invalid, Unsupported };
juce::String candidateStatusKey(CandidateStatus);
std::optional<CandidateStatus> candidateStatusFromKey(const juce::String&);

// State of the automatic analysis for one field. A manual override is a separate attribute
// (see OverrideState) and never replaces this state: a hand-set BPM stays effective while the
// automatic source may still be stale or conflicting.
enum class SmartState { NotAnalyzed, Queued, Analyzing, Unknown, Resolved, Ambiguous, Conflicting,
                        Unsupported, NotApplicable, Stale, Error, Default, Overridden };
juce::String smartStateKey(SmartState);
std::optional<SmartState> smartStateFromKey(const juce::String&);

// State of the whole smart record. NotAnalyzed/Queued/Analyzing describe the pipeline; Complete
// means a finished generation was published; Stale means its dependencies or versions changed;
// Error means the last attempt failed while the previous completed generation is retained.
enum class SmartPipelineState { NotAnalyzed, Queued, Analyzing, Complete, Stale, Error };
juce::String smartPipelineStateKey(SmartPipelineState);
std::optional<SmartPipelineState> smartPipelineStateFromKey(const juce::String&);

// How a field is changed by the user. None means the automatic value is effective.
enum class OverrideState { None, Value, Unknown, NotApplicable };
juce::String overrideStateKey(OverrideState);
std::optional<OverrideState> overrideStateFromKey(const juce::String&);

struct SmartCandidate
{
    SmartField field = SmartField::Type;
    juce::String source;      // filename, folder, wav_smpl, wav_inst, wav_acid, info, id3,
                              // aiff_basc, asd_warp, rx2_glob, dsp
    juce::String detail;      // raw field name or original value, kept for explanation
    juce::String textValue;   // Type/Structure/Key value, e.g. "C# minor"
    double numericValue = 0;  // BPM, MIDI pitch or MIDI root note
    double auxValue = 0;      // detune in cents for Pitch; unused by other fields
    CandidateStatus status = CandidateStatus::Valid;
    // Provenance. extractorVersion is the version that produced exactly this candidate.
    juce::String extractorId;
    int extractorVersion = 0;
    juce::String rawValue;    // original text/number before normalization
    juce::String inputKind;   // "audio", "asd" or "mapping"
    juce::String inputPath;   // file the candidate came from
    juce::String fingerprint; // size and mtime of that input at read time
    juce::String limitation;  // why the value may not be the musical truth
    bool variableTempo = false;
    int64_t extractedAt = 0;
};

struct SmartOverride
{
    bool cleared = false;      // explicit "unknown/not applicable", distinct from no override
    bool notApplicable = false;
    juce::String textValue;
    double numericValue = 0;
    double auxValue = 0;
    juce::String auxText;      // key mode, stored for queryable filters
};
using SmartOverrideMap = std::map<SmartField, SmartOverride>;

// Fingerprint of a file whose contents contributed candidates (the audio or a sidecar).
struct SmartInput
{
    juce::String kind; // "audio", "asd" or "mapping"
    juce::String path;
    int64_t size = 0, modified = 0;
};

// One extraction attempt per source. Storing "ran and found nothing" is what lets a later
// version bump re-run the extractor instead of silently keeping an empty result.
struct SmartExtraction
{
    juce::String source;      // name, wav, aiff, asd, rx2, dsp
    int extractorVersion = 0;
    juce::String status;      // ok, absent, error, unsupported
    juce::String error;
    int64_t updatedAt = 0;
};

// Effective catalog values for the library columns. Formatting and markers stay in the UI;
// this is the resolved value with the manual override applied.
struct SmartDisplay
{
    juce::String type, structure, key;
    double bpm = 0;
    bool hasBpm = false;
    bool hasPitch = false;
    int pitchMidi = -1;
    double pitchCents = 0;
    SmartState typeState = SmartState::Unknown;
    SmartState structureState = SmartState::Unknown;
    SmartState bpmState = SmartState::Unknown;
    SmartState keyState = SmartState::Unknown;
    SmartState pitchState = SmartState::Unknown;
    OverrideState typeOverride = OverrideState::None;
    OverrideState structureOverride = OverrideState::None;
    OverrideState bpmOverride = OverrideState::None;
    OverrideState keyOverride = OverrideState::None;
    OverrideState pitchOverride = OverrideState::None;
    SmartPipelineState pipelineState = SmartPipelineState::NotAnalyzed;
};

// Selection-bar text for the resolved pitch, with the same textual state markers as the other
// smart fields. Pure; the UI owns layout only.
juce::String pitchDisplayText(const SmartDisplay&);

struct SmartFailure
{
    juce::String code;        // input-changed, extract-error, publish-race, unsupported
    juce::String message;
    int attempts = 0;
    int64_t updatedAt = 0;
};

struct SmartResult
{
    juce::String type, structure;
    SmartState typeState = SmartState::Unknown, structureState = SmartState::Unknown;
    double bpm = 0;
    bool hasBpm = false;
    SmartState bpmState = SmartState::Unknown;
    int keyTonic = -1;         // 0 = C .. 11 = B; -1 means unknown
    juce::String keyMode;      // major, minor or empty when only the tonic is known
    SmartState keyState = SmartState::Unknown;
    // Pitch is kept apart from musical key: a detected note is not evidence of a mode.
    bool hasPitch = false;
    int pitchMidi = -1;
    double pitchCents = 0;
    SmartState pitchState = SmartState::Unknown;
    int rootNote = -1;         // sampler root note, never a musical key
    SmartState rootState = SmartState::Unknown;
    juce::String typeSource, structureSource, bpmSource, keySource, pitchSource, rootSource;
    OverrideState typeOverride = OverrideState::None, structureOverride = OverrideState::None;
    OverrideState bpmOverride = OverrideState::None, keyOverride = OverrideState::None;
    OverrideState pitchOverride = OverrideState::None, rootOverride = OverrideState::None;
    juce::String explanation;
    // Pipeline metadata. revision increments on every successful publication; a job publishes
    // only if it still expects the revision it read before starting.
    int64_t revision = 0;
    int64_t generation = 0;
    SmartPipelineState pipelineState = SmartPipelineState::NotAnalyzed;
    int extractorVersion = 0, resolverVersion = 0, dspVersion = 0;
    int64_t analyzedAt = 0;
    juce::String errorCode, errorDetail;
};
} // namespace sonicator
