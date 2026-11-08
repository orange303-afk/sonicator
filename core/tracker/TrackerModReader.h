#pragma once
#include "TrackerFormats.h"
namespace sonicator {
// Header-only MOD recognition shared by the catalog probe and the PCM reader. `head` may be a
// bounded prefix of the file; `fileSize` is the real size, used for the sample-data extent check.
struct ModHeader
{
    int sampleSlots = 0;               // 15 or 31
    int channels = 4;
    int songLength = 0, patternCount = 0;
    int64_t dataOffset = 0;            // first sample byte (after pattern data)
    juce::String title;
    juce::String variant;
    std::vector<TrackerSampleSlot> slots;
    juce::StringArray warnings;
};
bool readModHeader(const uint8_t* head, size_t headSize, int64_t fileSize, ModHeader& out);
bool parseMod(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error);
} // namespace sonicator
