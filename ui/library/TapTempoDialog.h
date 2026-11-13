#pragma once
#include "core/smart/TapTempo.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace sonicator
{
// Tap-tempo window. Space taps (it never starts playback by itself), Return applies, Esc cancels.
// The owner supplies the loop callbacks, so a batch selection can open the same window without
// loading audio into the preview.
class TapTempoDialog final : public juce::Component
{
public:
    explicit TapTempoDialog(juce::String titleText);
    ~TapTempoDialog() override = default;

    std::function<void()> onStartLoop, onStopLoop, onCancel;
    std::function<void(double bpm)> onApply;

    void tap();
    void setClockForTest(std::function<int64_t()> testClock) { clock = std::move(testClock); }
    double currentBpm() const { return tempo.bpm(); }
    bool keyPressed(const juce::KeyPress&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    static void launch(juce::Component* owner, juce::String titleText,
                       std::function<void()> startLoop, std::function<void()> stopLoop,
                       std::function<void(double)> apply);

private:
    TapTempo tempo;
    std::function<int64_t()> clock;
    juce::Label title, value;
    juce::TextButton tapButton { "Tap" }, loopButton { "Start loop" },
                     applyButton { "Apply" }, cancelButton { "Cancel" };
    bool loopRunning = false;

    void updateLabels();
    void apply();
    void cancel();
    void closeDialog();
};
} // namespace sonicator
