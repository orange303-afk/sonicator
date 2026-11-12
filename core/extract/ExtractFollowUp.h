#pragma once
#include <juce_core/juce_core.h>
namespace sonicator {
// What the library must do after a batch wrote files: register folders that live outside every
// existing root, then scan everything that was written. The scan list never contains a folder that
// a listed parent already covers.
struct ExtractFollowUp
{
    juce::StringArray newRoots;
    juce::StringArray scanFolders;
};
ExtractFollowUp planExtractFollowUp(const juce::StringArray& outputFolders,
                                    const juce::StringArray& existingRoots);
} // namespace sonicator
