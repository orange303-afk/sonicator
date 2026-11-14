#pragma once
#include "AudioEditWriter.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace sonicator::edit
{
// Audio settings and metadata for one streamed write. `frames` is the expected length and is used
// to verify the finished file.
struct AudioWriteSpec
{
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 16;
    int64_t frames = 0;
    bool floatingPoint = false;
    juce::StringPairArray metadata;
};

// Writes every frame through `feed`. The destination must not exist yet; a partial file is
// removed on failure and the finished file is reopened and verified against the spec.
using AudioFeed = std::function<bool(juce::AudioFormatWriter&)>;
SaveResult writeAudioStream(const juce::File& destination, const juce::String& extension,
                            const AudioWriteSpec&, const AudioFeed&, CancelCheck cancel = {});

// Permissions and extended attributes follow a replaced file, so an in-place edit does not lose
// them. Used by the atomic overwrite in AudioEditWriter and by the streaming normalizer.
void copyFileAttributes(const juce::File& from, const juce::File& to);
} // namespace sonicator::edit
