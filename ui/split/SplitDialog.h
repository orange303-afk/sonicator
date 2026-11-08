#pragma once
#include "core/settings/AppSettings.h"
#include "core/split/SplitService.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
namespace sonicator {
// Modal content for one split run: threshold, minimum silence, minimum part and output folder.
// Nothing is exported here; the owner starts SplitService from the accept callback.
class SplitDialog final : public juce::Component
{
public:
    SplitDialog(std::vector<juce::File> sources, const AppSettings&,
                std::function<void(SplitOptions)> onAccept);
    void paint(juce::Graphics&) override;
    void resized() override;
    SplitOptions currentOptions() const;
private:
    std::vector<juce::File> sources;
    juce::Label title, thresholdLabel, minSilenceLabel, minPartLabel, error;
    juce::Slider threshold, minSilence, minPart;
    juce::ToggleButton sameFolder { "Save next to each original file" };
    juce::TextEditor folder;
    juce::TextButton browse { "Browse..." }, accept { "Split" }, cancel { "Cancel" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::function<void(SplitOptions)> acceptCallback;
    void closeDialog();
};
} // namespace sonicator
