#include "FileClipboard.h"
namespace sonicator {
bool copyFilesToClipboard(const juce::Array<juce::File>& files)
{
    juce::StringArray paths;
    for (const auto& file : files) if (file.existsAsFile()) paths.add(file.getFullPathName());
    if (paths.isEmpty()) return false;
    juce::SystemClipboard::copyTextToClipboard(paths.joinIntoString("\n"));
    return false;
}
} // namespace sonicator
