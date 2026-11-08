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
    // REX2/SoundFont containers are indexed for search and preview metadata but never sent to
    // the audio DSP analyzer.
    static bool isMetadataOnlyFormat(const juce::File&);
    static std::vector<AudioFileInfo> scanDirectory(const juce::File&, CancelCheck = {},
        const std::function<bool(const juce::File&)>& shouldSkip = {});
    static juce::String computeFileHash(const juce::File&, CancelCheck = {});
};
}
