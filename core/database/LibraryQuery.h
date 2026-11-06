#pragma once
#include "SampleRecord.h"
#include <cstdint>
#include <vector>
namespace sonicator {
struct Tag
{
    int64_t id = 0;
    juce::String name;
    uint32_t colour = 0xff8a8f9c;
    bool builtin = false;
    int sampleCount = 0;
};
enum class LibrarySort { Name, Format, Duration, Rate, Channels, Status };
struct LibraryFilter
{
    juce::String text;
    juce::String folder;
    std::vector<int64_t> includeTags;
    std::vector<int64_t> excludeTags;
    bool matchAnyTag = false, starredOnly = false, hideDuplicates = false;
    juce::String duplicateHash; // show every member of this byte-identical group
    LibrarySort sort = LibrarySort::Name;
    bool ascending = true;
    bool hasTextOrTagFilter() const
    {
        return text.isNotEmpty() || ! includeTags.empty() || ! excludeTags.empty()
            || starredOnly || hideDuplicates || duplicateHash.isNotEmpty();
    }
};
struct LibraryPage
{
    std::vector<SampleRecord> samples;
    int total = 0;
};
} // namespace sonicator
