#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator
{
// A thin drag handle between two panels. It reports a delta from the position where the drag
// started, so the owner can clamp against live bounds without the handle drifting with its own
// moving rectangle. Double-click asks the owner to reset just this split.
class PanelSplitter final : public juce::Component
{
public:
    enum class Axis { columns, rows };
    explicit PanelSplitter(Axis axis, juce::String panelId);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    std::function<int()> getPreferred;
    // Called once at mouse-down with the size the drag starts from.
    std::function<void(int)> onDragBegin;
    // Called with the pixel delta from the drag start, positive right/down.
    std::function<void(int)> onDrag;
    std::function<void()> onCommit;
    std::function<void()> onReset;
    juce::String panelId() const { return id; }

private:
    Axis axis;
    juce::String id;
    int dragStart = 0;
    bool dragging = false;
};
} // namespace sonicator
