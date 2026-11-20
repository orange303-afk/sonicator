#pragma once
#include "AudioEditBuffer.h"
#include "core/files/FileDisposal.h"
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

// Publishes an already written and validated temporary file at `target`.
// The old file is renamed beside the target before the new one takes its place, so a failed
// publication restores it. After a successful publication the old file goes to Trash or is
// deleted per `policy`; if that fails, both files remain and `retainedOriginal` names the old
// one, so the caller reports the replacement as done but not fully cleaned up.
struct PublishResult
{
    bool success = false;
    bool disposalFailed = false;
    juce::String error;
    juce::File retainedOriginal;
};
PublishResult publishReplacement(const juce::File& target, juce::TemporaryFile& prepared,
                                 files::DisposalPolicy policy);
// Restores or completes a replace-in-place operation interrupted between its two renames.
// The journal lives beside the settings file; an explicit path is for tests. Returns a line for
// the log, empty when there was nothing to do.
juce::String recoverInterruptedReplacement(const juce::File& journal = {});

// Replaces the target: the new audio is written to a temporary file in the same folder,
// validated, given the original permissions and extended attributes, and then published.
// A failure leaves the original byte-identical. The replaced original is disposed of per policy
// (Trash by default).
SaveResult overwrite(const juce::File& target, const AudioEditBuffer&, CancelCheck cancel = {},
                     files::DisposalPolicy policy = files::DisposalPolicy::Trash);

// Writes a new file; the source is untouched.
SaveResult saveCopy(const juce::File& original, const juce::String& stem,
                    const AudioEditBuffer&, CancelCheck cancel = {});
} // namespace sonicator::edit
