#include "FileDisposal.h"

namespace sonicator::files
{
juce::String disposalPolicyToString(const DisposalPolicy policy)
{
    return policy == DisposalPolicy::DeletePermanently ? "delete" : "trash";
}

DisposalPolicy disposalPolicyFromString(const juce::String& text, const DisposalPolicy fallback)
{
    const auto value = text.trim().toLowerCase();
    if (value == "delete" || value == "permanent" || value == "permanently")
        return DisposalPolicy::DeletePermanently;
    if (value == "trash" || value == "recycle") return DisposalPolicy::Trash;
    return fallback;
}

juce::String disposalPolicyLabel(const DisposalPolicy policy)
{
    return policy == DisposalPolicy::DeletePermanently ? "Delete permanently" : "Move to Trash";
}

DisposalOutcome disposeFile(const juce::File& file, const DisposalPolicy policy)
{
    if (! file.exists()) return { false, "File not found: " + file.getFullPathName(), file };
    if (file.isDirectory())
        return { false, "Not a file: " + file.getFullPathName(), file };
    // A link is not the file the user selected; deleting through it would remove the original.
    if (file.isSymbolicLink())
        return { false, "Symbolic link is left in place: " + file.getFullPathName(), file };
    if (policy == DisposalPolicy::Trash)
    {
        // Never a fallback to deleteFile(): a failed Trash move keeps the file and reports it.
        if (! file.moveToTrash())
            return { false, "Could not move the file to Trash: " + file.getFullPathName(), file };
        return { true, {}, {} };
    }
    if (! file.deleteFile())
        return { false, "Could not delete the file: " + file.getFullPathName(), file };
    return { true, {}, {} };
}
} // namespace sonicator::files
