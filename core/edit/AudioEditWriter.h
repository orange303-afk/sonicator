#pragma once
#include "AudioEditBuffer.h"
#include "core/scanner/FileScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace sonicator::edit
{
struct SaveResult
{
    bool success = false;
    juce::File file;
    juce::String error;
};

// WAV, AIFF and FLAC can be written back over the original; anything else can only be copied.
bool canOverwriteInPlace(const juce::File& target);

// `<stem> edit<ext>` beside the original, then `edit 2`, `edit 3`, ... Never an existing path.
// The extension follows the original when it can be overwritten, otherwise WAV.
juce::File uniqueCopyPath(const juce::File& original, const juce::String& stem);

// Replaces the target atomically: the new audio is written to a temporary file in the same
// folder, validated, given the original permissions and extended attributes, and then renamed
// over the target. A failure leaves the original byte-identical.
SaveResult overwrite(const juce::File& target, const AudioEditBuffer&, CancelCheck cancel = {});

// Writes a new file; the source is untouched.
SaveResult saveCopy(const juce::File& original, const juce::String& stem,
                    const AudioEditBuffer&, CancelCheck cancel = {});
} // namespace sonicator::edit
