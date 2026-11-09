#pragma once
#include "core/extract/ExtractService.h"
#include "core/settings/AppSettings.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
namespace sonicator {
// Content for one extract run. Like SplitDialog it never touches the filesystem itself: the owner
// starts ExtractService from the accept callback and feeds progress back through setProgress().
// The dialog stays open while the job runs, then replaces Extract/Cancel with OK and can reveal
// the per-sample problems.
class ExtractDialog final : public juce::Component
{
public:
    ExtractDialog(std::vector<juce::File> sources, const AppSettings&,
                  std::function<bool(ExtractOptions)> onAccept);
    void paint(juce::Graphics&) override;
    void resized() override;
    ExtractOptions currentOptions() const;
    void enterRunningState();
    void setProgress(const ExtractProgress&, double elapsedSeconds);
    void showFinished(const juce::String& summary, const juce::String& details, double fraction);
    bool isRunning() const { return running; }
    bool isFinished() const { return finished; }
    std::function<void()> onCancel, onOk;
private:
    std::vector<juce::File> sources;
    juce::Label title, error, progressLabel, resultLabel;
    juce::ToggleButton sameFolder { "Save next to each original file" };
    juce::ToggleButton normalize { "Normalize each sample to -0.1 dBFS" };
    juce::ToggleButton details { "Show details" };
    juce::TextEditor folder, problems;
    juce::TextButton browse { "Browse..." }, accept { "Extract" }, cancel { "Cancel" }, ok { "OK" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    std::unique_ptr<juce::FileChooser> chooser;
    std::function<bool(ExtractOptions)> acceptCallback;
    bool running = false, finished = false;
    void setInputsEnabled(bool);
    void updateButtons();
    void closeDialog();
};
} // namespace sonicator
