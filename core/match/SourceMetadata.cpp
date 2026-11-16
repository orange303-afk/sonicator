#include "core/match/SourceMetadata.h"

namespace sonicator
{
MetadataState metadataStateFromSmart (SmartState state)
{
    switch (state)
    {
        case SmartState::Resolved:
        case SmartState::Default:
        case SmartState::Overridden: return MetadataState::known;
        case SmartState::Stale: return MetadataState::stale;
        case SmartState::Ambiguous:
        case SmartState::Conflicting: return MetadataState::conflicting;
        case SmartState::NotApplicable: return MetadataState::notApplicable;
        case SmartState::Unknown: return MetadataState::unknown;
    }
    return MetadataState::unknown;
}

MatchStructure matchStructureFromText (const juce::String& structure)
{
    const auto text = structure.trim().toLowerCase();
    if (text == "loop" || text == "loops") return MatchStructure::loop;
    if (text == "one-shot" || text == "one shot" || text == "oneshot")
        return MatchStructure::oneShot;
    return MatchStructure::unknown;
}

MatchSource sourceFromSmartResult (const SmartResult& result, const juce::String& identity,
                                   int64_t startFrame, int64_t sourceFrames, double sampleRate,
                                   int channels)
{
    MatchSource source;
    source.identity = identity;
    source.startFrame = startFrame;
    source.sourceFrames = sourceFrames;
    source.sampleRate = sampleRate;
    source.channels = channels;
    source.structure = matchStructureFromText (result.structure);
    source.bpm = result.bpm;
    source.bpmState = metadataStateFromSmart (result.bpmState);
    source.keyTonic = result.keyTonic;
    source.keyMode = result.keyMode.trim().toLowerCase();
    source.keyState = metadataStateFromSmart (result.keyState);
    source.hasPitch = result.hasPitch;
    source.pitchMidi = result.pitchMidi;
    source.pitchCents = result.pitchCents;
    source.pitchState = metadataStateFromSmart (result.pitchState);
    source.rootNote = result.rootNote;
    source.rootState = metadataStateFromSmart (result.rootState);
    source.metadataRevision = result.revision;
    return source;
}
} // namespace sonicator
