#pragma once
#include "SmartTypes.h"
namespace sonicator {
struct AsdMetadataResult
{
    std::vector<SmartCandidate> candidates;
    juce::String error;
    int version = 0;
    int64_t frameCount = 0;
    int warpMarkerCount = 0;
    bool supported = false;
};
// Reads Ableton Live .asd sidecars without trusting field presence: warp markers are validated
// and AufTaktData's tempo estimate is only used when IsSet is true and the value is plausible.
AsdMetadataResult extractAsdMetadata(const juce::File&);
} // namespace sonicator
