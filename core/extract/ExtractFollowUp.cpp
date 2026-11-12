#include "ExtractFollowUp.h"
namespace sonicator {
namespace {
bool inside(const juce::String& folder, const juce::String& root)
{
    const juce::File file(folder), parent(root);
    return file == parent || file.isAChildOf(parent);
}
} // namespace
ExtractFollowUp planExtractFollowUp(const juce::StringArray& outputFolders,
                                    const juce::StringArray& existingRoots)
{
    ExtractFollowUp plan;
    juce::StringArray folders;
    for (const auto& folder : outputFolders)
        if (folder.isNotEmpty() && ! folders.contains(folder)) folders.add(folder);
    folders.sort(true); // parents before their children
    juce::StringArray kept;
    for (const auto& folder : folders)
    {
        bool covered = false;
        for (const auto& parent : kept)
            if (juce::File(folder).isAChildOf(juce::File(parent))) { covered = true; break; }
        if (! covered) kept.add(folder);
    }
    for (const auto& folder : kept)
    {
        bool inRoot = false;
        for (const auto& root : existingRoots)
            if (inside(folder, root)) { inRoot = true; break; }
        if (! inRoot) plan.newRoots.add(folder);
        plan.scanFolders.add(folder);
    }
    return plan;
}
} // namespace sonicator
