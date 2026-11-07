#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <vector>
namespace sonicator {
// Fields that Phase 2 derives from metadata and DSP. They are deliberately separate from
// SampleRecord: measured Phase 1 descriptors and user tags must not be rewritten by them.
enum class SmartField { Type, Structure, Bpm, Key, RootNote };
inline constexpr int smartFieldCount = 5;
juce::String smartFieldKey(SmartField);
std::optional<SmartField> smartFieldFromKey(const juce::String&);

// Invalid means "the field was present but its value is not usable"; Unsupported means the
// container/version is not handled. Both stay visible for provenance, but never resolve a value.
enum class CandidateStatus { Valid, DefaultValue, Invalid, Unsupported };
juce::String candidateStatusKey(CandidateStatus);
std::optional<CandidateStatus> candidateStatusFromKey(const juce::String&);

enum class SmartState { Unknown, Resolved, Ambiguous, Conflicting, Unsupported, Default, Overridden };
juce::String smartStateKey(SmartState);

struct SmartCandidate
{
    SmartField field = SmartField::Type;
    juce::String source;      // filename, folder, wav_smpl, wav_inst, wav_acid, info, id3,
                              // aiff_basc, asd_warp, rx2_glob, dsp
    juce::String detail;      // raw field name or original value, kept for explanation
    juce::String textValue;   // Type/Structure/Key value, e.g. "C# minor"
    double numericValue = 0;  // BPM or MIDI root note
    CandidateStatus status = CandidateStatus::Valid;
};

struct SmartOverride
{
    bool cleared = false;      // explicit "unknown/not applicable", distinct from no override
    juce::String textValue;
    double numericValue = 0;
};
using SmartOverrideMap = std::map<SmartField, SmartOverride>;
// Fingerprint of a file whose contents contributed candidates (the audio or a sidecar).
struct SmartInput
{
    juce::String kind; // "audio" or "asd"
    juce::String path;
    int64_t size = 0, modified = 0;
};

struct SmartResult
{
    juce::String type, structure;
    SmartState typeState = SmartState::Unknown, structureState = SmartState::Unknown;
    double bpm = 0;
    bool hasBpm = false;
    SmartState bpmState = SmartState::Unknown;
    int keyTonic = -1;         // 0 = C .. 11 = B; -1 means unknown
    juce::String keyMode;      // major, minor or unknown
    SmartState keyState = SmartState::Unknown;
    int rootNote = -1;         // sampler root note, never a musical key
    SmartState rootState = SmartState::Unknown;
    juce::String typeSource, structureSource, bpmSource, keySource, rootSource;
    juce::String explanation;
};
} // namespace sonicator
