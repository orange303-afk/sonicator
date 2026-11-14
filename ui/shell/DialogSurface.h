#pragma once
#include "DesignTokens.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator::dialogs
{
// One frame for every dialog: the panel surface, its 20 px margins, a title line, a footer row of
// buttons and three kinds of body row. Dialogs keep their own labels and controls inside it.
class Surface
{
public:
    // Paints only the panel and its border. Each dialog keeps its own title label, so its text,
    // tooltips and tests are unchanged.
    void paint(juce::Graphics&, juce::Rectangle<int> bounds) const;

    static juce::Rectangle<int> contentArea(juce::Rectangle<int> bounds);  // bounds reduced by 20
    static juce::Rectangle<int> titleArea(juce::Rectangle<int> bounds);    // top 24 px of the content
    static juce::Rectangle<int> footerArea(juce::Rectangle<int> bounds);   // bottom 32 px of the content

    // One 30 px row: the label takes 140 px, the control the rest. Advances `area`.
    static void placeControlRow(juce::Rectangle<int>& area, juce::Component& label, juce::Component& control);
    // The same row for a slider: its text box (90 px) is inside the slider, so it needs the width.
    static void placeSliderRow(juce::Rectangle<int>& area, juce::Component& label, juce::Slider& slider);
    // A 24 px check row with the LookAndFeel's tick. Advances `area`.
    static void placeCheck(juce::Rectangle<int>& area, juce::Component& toggle);
};
} // namespace sonicator::dialogs
