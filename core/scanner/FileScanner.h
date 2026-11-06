#pragma once
#include <juce_cryptography/juce_cryptography.h>
#include <functional>
#include <vector>
namespace sonicator {
using CancelCheck = std::function<bool()>;
struct AudioFileInfo
{
    juce::File file;
    int64_t fileSize = 0, dateModified = 0;
};
class FileScanner
{
public:
    static bool isSupportedFormat(const juce::File&);
    static std::vector<AudioFileInfo> scanDirectory(const juce::File&, CancelCheck = {});
    static juce::String computeFileHash(const juce::File&, CancelCheck = {});
};
}
