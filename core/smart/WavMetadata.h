#pragma once
#include "SmartTypes.h"
namespace sonicator {
struct WavFileInfo
{
    int channels = 0, bitsPerSample = 0;
    double sampleRate = 0;
    int64_t frames = 0;
    bool rf64 = false;
};
struct WavMetadataResult
{
    std::vector<SmartCandidate> candidates;
    WavFileInfo info;
    juce::String error; // structural problem; candidates parsed before it are still returned
};
// Reads metadata chunks with seeks only; never decodes PCM. Traverses the container to the end,
// including chunks after 'data', and stops safely on a bad size or a short read.
WavMetadataResult extractWavMetadata(const juce::File&);
} // namespace sonicator
