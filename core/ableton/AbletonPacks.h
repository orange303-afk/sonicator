#pragma once
#include <juce_core/juce_core.h>
namespace sonicator {
class SampleRepository;
class SettingsStore;
struct AbletonPreviewInfo
{
    bool isPreview = false;
    juce::String source; // rack, clip or device name without the preview extension
    juce::String kind;   // Clip, Rack, Device or Preview
};
// A rendered OGG demo under "<Pack>/Ableton Folder Info/Previews/...".
AbletonPreviewInfo abletonPreviewInfo(const juce::File&);
// Protected AIFC ("able" compression, Ableton Content). It can only be decoded by Live.
bool isAbletonProtectedSample(const juce::File&);
// One-time registration of the installed factory packs folder. Returns true only when this call
// added the root, so a folder removed by the user is not re-added on later launches.
bool registerAbletonPacks(SampleRepository&, const SettingsStore&, const juce::File& root);
}
