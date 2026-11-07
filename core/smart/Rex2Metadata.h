#pragma once
#include "SmartTypes.h"
namespace sonicator {
struct Rex2Slice
{
    int64_t start = 0, length = 0;
    int flags = 0;
};
struct Rex2Info
{
    double previewTempo = 0, originalTempo = 0;
    int channels = 0, format = 0, sampleRate = 0;
    int64_t frames = 0, loopStart = 0, loopEnd = 0, fileSize = 0;
    int64_t sdatOffset = 0, sdatSize = 0;
    int declaredSlices = 0;
    std::vector<Rex2Slice> slices; // playable or not; callers decide from flags and length
};
struct Rex2MetadataResult
{
    Rex2Info info;
    std::vector<SmartCandidate> candidates;
    juce::String error;
    int playableSlices = 0, mutedSlices = 0, badSlices = 0;
    bool truncated = false;
};
// Reads the CAT/REX2 container and slice boundaries. The DWOP payload is not decoded here.
Rex2MetadataResult extractRex2Metadata(const juce::File&);
} // namespace sonicator
