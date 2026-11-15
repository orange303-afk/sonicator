#pragma once
#include "BatchTypes.h"

namespace sonicator::batch
{
struct TextTags
{
    juce::String title, artist, album, genre, date, comment, track;
    bool any() const
    {
        return title.isNotEmpty() || artist.isNotEmpty() || album.isNotEmpty()
            || genre.isNotEmpty() || date.isNotEmpty() || comment.isNotEmpty()
            || track.isNotEmpty();
    }
};

struct MetadataPlan
{
    juce::StringPairArray values;
    TextTags tags;
    std::vector<Issue> warnings;
    bool timeIndexedDropped = false;
};

// v1 metadata policy: a verified WAV->WAV whitelist only. Sampler root survives; loop, cue and
// BWF time references are dropped when the temporal grid changed (Trim or SRC). AIFF/FLAC and
// cross-container writes report metadata_not_preserved instead of inventing values.
MetadataPlan prepareMetadata(const juce::AudioFormatReader& source, Format output, bool timeChanged);
} // namespace sonicator::batch
