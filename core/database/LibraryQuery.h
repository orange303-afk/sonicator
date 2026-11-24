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
enum class LibrarySort { Name, Format, Duration, Rate, Channels, Status, Type, Bpm, Key, Genre };
struct LibraryFilter
{
    juce::String text;
    juce::String folder;
    std::vector<int64_t> includeTags;
    std::vector<int64_t> excludeTags;
    bool matchAnyTag = false, starredOnly = false, hideDuplicates = false, hideEmpty = false,
         ignoreSymlinks = false;
    // Confirmed program structure, which is independent of tempo: a loop without a known BPM is
    // still a loop. Both on shows every row, unclassified files included; asking for one category
    // shows only that category, and asking for neither leaves the rows that are neither.
    bool showLoops = true, showOneShots = true;
    // Effective Type values (a manual override wins over the analyzer). Empty means every type.
    std::vector<juce::String> types;
    bool showIgnored = false; // reveal rows hidden by ignored_paths; a view toggle, not a filter
    juce::String duplicateHash; // show every member of this byte-identical group
    LibrarySort sort = LibrarySort::Name;
    bool ascending = true;
    bool hasTextOrTagFilter() const
    {
        return text.isNotEmpty() || ! includeTags.empty() || ! excludeTags.empty()
            || starredOnly || hideDuplicates || hideEmpty || ignoreSymlinks
            || ! showLoops || ! showOneShots || ! types.empty()
            || duplicateHash.isNotEmpty();
    }
};
struct LibraryPage
{
    std::vector<SampleRecord> samples;
    int total = 0;
};
} // namespace sonicator
