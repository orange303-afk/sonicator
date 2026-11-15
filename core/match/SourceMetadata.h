#pragma once
#include "MatchTypes.h"
#include "core/smart/SmartTypes.h"

namespace sonicator
{
// Maps a resolved catalog result into the pure policy input. Values come from SmartResult fields,
// never from a formatted caption: a displayed "120 BPM ~" marker says nothing about trust.
MatchSource sourceFromSmartResult (const SmartResult& result, const juce::String& identity,
                                   int64_t startFrame, int64_t sourceFrames, double sampleRate,
                                   int channels);

// The library stores a loop as "loop" and a one-shot as "one-shot". "phrase" and every other
// word stay unknown: the policy only stretches an explicit loop.
MatchStructure matchStructureFromText (const juce::String& structure);

MetadataState metadataStateFromSmart (SmartState state);
} // namespace sonicator
