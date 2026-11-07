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
        case SmartState::Unknown: return "unknown";
        case SmartState::Resolved: return "resolved";
        case SmartState::Ambiguous: return "ambiguous";
        case SmartState::Conflicting: return "conflicting";
        case SmartState::Unsupported: return "unsupported";
        case SmartState::Default: return "default";
        case SmartState::Overridden: return "overridden";
    }
    return {};
}
} // namespace sonicator
