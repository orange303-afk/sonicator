#pragma once
#include "core/database/SampleRecord.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
namespace sonicator {
// Builds and starts external file drags for library rows. The OS receives standard file URLs and
// canMoveFiles is always false, so a target copies or imports and never moves a library original.
class ExternalFileDrag
{
public:
    using DragFunction = std::function<bool(const juce::StringArray&, bool, juce::Component*)>;
    static juce::StringArray existingPaths(const juce::StringArray& candidates);
    static juce::StringArray selectedPaths(const std::vector<SampleRecord>& visible,
                                           const std::vector<int>& selectedRows);
    static bool exceedsThreshold(int dx, int dy);
    // Where .ebl rows are turned into WAV for the OS drag.
    static juce::File dragCacheFolder();
    // EBL paths become 16-bit WAVs in cacheDir (PCM preserved, loop written into smpl); other
    // paths are returned unchanged and in order. A file that cannot be converted is skipped and
    // named in `problems`, so one broken sample does not drop the rest of the selection.
    static juce::StringArray materialiseForDrag(const juce::StringArray& paths,
                                                const juce::File& cacheDir,
                                                juce::StringArray& problems);
    static bool start(const juce::StringArray& candidates, juce::Component* source,
                      DragFunction drag = {});
};
} // namespace sonicator
