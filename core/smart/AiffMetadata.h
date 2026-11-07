#pragma once
#include "SmartTypes.h"
namespace sonicator {
struct AiffFileInfo
{
    int channels = 0, bitsPerSample = 0;
    double sampleRate = 0, duration = 0;
    int64_t frames = 0;
};
struct AiffMetadataResult
{
    std::vector<SmartCandidate> candidates;
    AiffFileInfo info;
    juce::String error;
};
// Apple Loops metadata: basc (beats/root/signature), cate/coll categories, INST root, MARK.
// BPM is derived from beats and the actual COMM duration, never from the file name.
AiffMetadataResult extractAiffMetadata(const juce::File&);
} // namespace sonicator
