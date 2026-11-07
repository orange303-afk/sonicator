#include "SmartTypes.h"
namespace sonicator {
juce::String smartFieldKey(SmartField field)
{
    switch (field)
    {
        case SmartField::Type: return "type";
        case SmartField::Structure: return "structure";
        case SmartField::Bpm: return "bpm";
        case SmartField::Key: return "key";
        case SmartField::Pitch: return "pitch";
        case SmartField::RootNote: return "root_note";
    }
    return {};
}
std::optional<SmartField> smartFieldFromKey(const juce::String& key)
{
    if (key == "type") return SmartField::Type;
    if (key == "structure") return SmartField::Structure;
    if (key == "bpm") return SmartField::Bpm;
    if (key == "key") return SmartField::Key;
    if (key == "pitch") return SmartField::Pitch;
    if (key == "root_note") return SmartField::RootNote;
    return std::nullopt;
}
juce::String candidateStatusKey(CandidateStatus status)
{
    switch (status)
    {
        case CandidateStatus::Valid: return "valid";
        case CandidateStatus::DefaultValue: return "default";
        case CandidateStatus::Invalid: return "invalid";
        case CandidateStatus::Unsupported: return "unsupported";
    }
    return {};
}
std::optional<CandidateStatus> candidateStatusFromKey(const juce::String& key)
{
    if (key == "valid") return CandidateStatus::Valid;
    if (key == "default") return CandidateStatus::DefaultValue;
    if (key == "invalid") return CandidateStatus::Invalid;
    if (key == "unsupported") return CandidateStatus::Unsupported;
    return std::nullopt;
}
juce::String smartStateKey(SmartState state)
{
    switch (state)
    {
        case SmartState::NotAnalyzed: return "not_analyzed";
        case SmartState::Queued: return "queued";
        case SmartState::Analyzing: return "analyzing";
        case SmartState::Unknown: return "unknown";
        case SmartState::Resolved: return "resolved";
        case SmartState::Ambiguous: return "ambiguous";
        case SmartState::Conflicting: return "conflicting";
        case SmartState::Unsupported: return "unsupported";
        case SmartState::NotApplicable: return "not_applicable";
        case SmartState::Stale: return "stale";
        case SmartState::Error: return "error";
        case SmartState::Default: return "default";
        case SmartState::Overridden: return "overridden";
    }
    return {};
}
std::optional<SmartState> smartStateFromKey(const juce::String& key)
{
    if (key == "not_analyzed") return SmartState::NotAnalyzed;
    if (key == "queued") return SmartState::Queued;
    if (key == "analyzing") return SmartState::Analyzing;
    if (key == "unknown") return SmartState::Unknown;
    if (key == "resolved") return SmartState::Resolved;
    if (key == "ambiguous") return SmartState::Ambiguous;
    if (key == "conflicting") return SmartState::Conflicting;
    if (key == "unsupported") return SmartState::Unsupported;
    if (key == "not_applicable") return SmartState::NotApplicable;
    if (key == "stale") return SmartState::Stale;
    if (key == "error") return SmartState::Error;
    if (key == "default") return SmartState::Default;
    if (key == "overridden") return SmartState::Overridden;
    return std::nullopt;
}
juce::String smartPipelineStateKey(SmartPipelineState state)
{
    switch (state)
    {
        case SmartPipelineState::NotAnalyzed: return "not_analyzed";
        case SmartPipelineState::Queued: return "queued";
        case SmartPipelineState::Analyzing: return "analyzing";
        case SmartPipelineState::Complete: return "complete";
        case SmartPipelineState::Stale: return "stale";
        case SmartPipelineState::Error: return "error";
    }
    return {};
}
std::optional<SmartPipelineState> smartPipelineStateFromKey(const juce::String& key)
{
    if (key == "not_analyzed") return SmartPipelineState::NotAnalyzed;
    if (key == "queued") return SmartPipelineState::Queued;
    if (key == "analyzing") return SmartPipelineState::Analyzing;
    if (key == "complete") return SmartPipelineState::Complete;
    if (key == "stale") return SmartPipelineState::Stale;
    if (key == "error") return SmartPipelineState::Error;
    return std::nullopt;
}
juce::String overrideStateKey(OverrideState state)
{
    switch (state)
    {
        case OverrideState::None: return "none";
        case OverrideState::Value: return "value";
        case OverrideState::Unknown: return "unknown";
        case OverrideState::NotApplicable: return "not_applicable";
    }
    return {};
}
std::optional<OverrideState> overrideStateFromKey(const juce::String& key)
{
    if (key == "none") return OverrideState::None;
    if (key == "value") return OverrideState::Value;
    if (key == "unknown") return OverrideState::Unknown;
    if (key == "not_applicable") return OverrideState::NotApplicable;
    return std::nullopt;
}
} // namespace sonicator
