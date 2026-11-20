#pragma once
#include <juce_core/juce_core.h>

namespace sonicator::files
{
// What happens to a file that an operation has replaced or removed.
// Trash is recoverable through the operating system; DeletePermanently is not.
enum class DisposalPolicy { Trash, DeletePermanently };

juce::String disposalPolicyToString(DisposalPolicy);
DisposalPolicy disposalPolicyFromString(const juce::String&, DisposalPolicy fallback);
// "Move to Trash" / "Delete permanently" for a combo box or a confirmation line.
juce::String disposalPolicyLabel(DisposalPolicy);

struct DisposalOutcome
{
    bool ok = false;
    juce::String error;
    // Set when the file was neither trashed nor deleted; the path is still on disk.
    juce::File retained;
};

// Removes one regular file: to the platform Trash / Recycle Bin, or permanently.
// Directories and symbolic links are refused, so a link never decides what its target loses.
// A failed Trash move never falls back to permanent deletion.
DisposalOutcome disposeFile(const juce::File& file, DisposalPolicy policy);
} // namespace sonicator::files
