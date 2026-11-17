#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace sonicator
{
// One row at the top of the row context menu. The menu owns the component; hovering it for a
// moment triggers the item, which closes the menu and opens the round tag menu. The shared point
// records where the pill was on screen so the menu callback can open the ring from it.
class TagsMenuPill final : public juce::PopupMenu::CustomComponent, private juce::Timer
{
public:
    TagsMenuPill(std::shared_ptr<juce::Point<int>> screenCentre, std::function<void()> hovered);
    void getIdealSize(int& idealWidth, int& idealHeight) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    std::shared_ptr<juce::Point<int>> centre;
    std::function<void()> onHover;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TagsMenuPill)
};
} // namespace sonicator
