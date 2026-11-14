#pragma once
#include "core/settings/AppSettings.h"
#include "core/split/SplitService.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
namespace sonicator {
// Content for one split run. It never exports audio itself: the owner starts SplitService from
// the accept callback and feeds progress back through setProgress(). The dialog stays open while
// the job runs, then replaces Split/Cancel with OK.
class SplitDialog final : public juce::Component
{
public:
    SplitDialog(std::vector<juce::File> sources, const AppSettings&,
                std::function<bool(SplitOptions)> onAccept);
    void paint(juce::Graphics&) override;
    void resized() override;
    SplitOptions currentOptions() const;
    void enterRunningState();
    void setProgress(const SplitProgress&, double elapsedSeconds);
    void showFinished(const juce::String& summary, double fraction);
    bool isRunning() const { return running; }
    bool isFinished() const { return finished; }
    std::function<void()> onCancel, onOk;
private:
    std::vector<juce::File> sources;
    juce::Label title, thresholdLabel, minSilenceLabel, minPartLabel, error;
    juce::Slider threshold, minSilence, minPart;
    juce::ToggleButton sameFolder { "Save next to each original file" };
    juce::ToggleButton moveToTrash { "Move originals to Trash after a successful split" };
    juce::ToggleButton normalize { "Normalize each part to -0.1 dBFS" };
    juce::TextEditor folder;
    juce::TextButton browse { "Browse..." }, accept { "Split" }, cancel { "Cancel" }, ok { "OK" };
    juce::Label progressLabel, resultLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    std::unique_ptr<juce::FileChooser> chooser;
    std::function<bool(SplitOptions)> acceptCallback;
    bool running = false, finished = false;
    void setInputsEnabled(bool);
    void closeDialog();
    void updateButtons();
};
} // namespace sonicator
