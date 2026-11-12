#pragma once
#include "core/disc/DiscFilesystem.h"
#include <juce_core/juce_core.h>
#include <vector>
namespace sonicator {
// One sample record of a disc image, identified the same way the browser identifies it, so a
// selected row and a whole-image walk decode exactly the same audio.
struct DiscExtractEntry
{
    DiscEntryId id, partner;
    juce::String name;
};
// `entries` empty means the whole image. `image` may be a .cue, a .bin/.img with a sibling .cue,
// or an ISO image.
struct DiscExtractJob
{
    juce::File image;
    std::vector<DiscExtractEntry> entries;
};
// True when the file is something the extractor can open as a disc image. A .bin/.img with a
// sibling .cue is accepted, because the opener resolves that cue itself.
bool isDiscExtractSource(const juce::File&);
} // namespace sonicator
