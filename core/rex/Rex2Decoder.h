#pragma once
#include <juce_core/juce_core.h>
#include <vector>
namespace sonicator {
struct Rex2Audio
{
    int channels = 0, sampleRate = 0, bitDepth = 0;
    int64_t frames = 0;
    std::vector<float> left, right;
};
// Decodes the DWOP bitstream in an REX2 SDAT chunk into float PCM. Read-only and bounded.
bool decodeRex2Audio(const juce::File&, Rex2Audio&, juce::String& error);
} // namespace sonicator
