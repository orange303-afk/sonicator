#include "PanelSplitter.h"
#include "Theme.h"

namespace sonicator
{
PanelSplitter::PanelSplitter(Axis splitAxis, juce::String panelId)
    : axis(splitAxis), id(std::move(panelId))
{
    setMouseCursor(axis == Axis::columns ? juce::MouseCursor::LeftRightResizeCursor
                                         : juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void PanelSplitter::paint(juce::Graphics& g)
{
    // A short rounded grip in the middle, not a full-length rule: it reads as a handle.
    const auto area = getLocalBounds().toFloat();
    juce::Rectangle<float> grip;
    if (axis == Axis::columns)
        grip = area.withSizeKeepingCentre(2.0f, juce::jmin(28.0f, area.getHeight() * 0.25f));
    else
        grip = area.withSizeKeepingCentre(juce::jmin(28.0f, area.getWidth() * 0.25f), 2.0f);
    g.setColour(isMouseOver() || dragging ? theme::accent : theme::line);
    g.fillRoundedRectangle(grip, 1.0f);
}

void PanelSplitter::mouseDown(const juce::MouseEvent&)
{
    dragStart = getPreferred != nullptr ? getPreferred() : 0;
    dragging = true;
    if (onDragBegin != nullptr) onDragBegin(dragStart);
    repaint();
}

void PanelSplitter::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging || onDrag == nullptr) return;
    onDrag(axis == Axis::columns ? event.getDistanceFromDragStartX()
                                 : event.getDistanceFromDragStartY());
}

void PanelSplitter::mouseUp(const juce::MouseEvent&)
{
    if (!dragging) return;
    dragging = false;
    repaint();
    if (onCommit != nullptr) onCommit();
}

void PanelSplitter::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onReset != nullptr) onReset();
}

void PanelSplitter::mouseEnter(const juce::MouseEvent&) { repaint(); }
void PanelSplitter::mouseExit(const juce::MouseEvent&) { repaint(); }
} // namespace sonicator
