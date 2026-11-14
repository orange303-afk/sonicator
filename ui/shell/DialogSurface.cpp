#include "DialogSurface.h"
#include "Theme.h"

namespace sonicator::dialogs
{
void Surface::paint(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    const auto area = bounds.toFloat();
    g.setColour(theme::panel);
    g.fillRoundedRectangle(area, studio::dialogRadius);
    g.setColour(theme::line);
    g.drawRoundedRectangle(area.reduced(0.5f), studio::dialogRadius, 1.0f);
}

juce::Rectangle<int> Surface::contentArea(juce::Rectangle<int> bounds)
{
    return bounds.reduced(20);
}

juce::Rectangle<int> Surface::titleArea(juce::Rectangle<int> bounds)
{
    return contentArea(bounds).removeFromTop(24);
}

juce::Rectangle<int> Surface::footerArea(juce::Rectangle<int> bounds)
{
    return contentArea(bounds).removeFromBottom(studio::iconButtonSize);
}

void Surface::placeControlRow(juce::Rectangle<int>& area, juce::Component& label, juce::Component& control)
{
    auto row = area.removeFromTop(studio::controlHeight);
    label.setBounds(row.removeFromLeft(140));
    control.setBounds(row);
    area.removeFromTop(studio::spacingTight);
}

void Surface::placeSliderRow(juce::Rectangle<int>& area, juce::Component& label, juce::Slider& slider)
{
    placeControlRow(area, label, slider);
}

void Surface::placeCheck(juce::Rectangle<int>& area, juce::Component& toggle)
{
    toggle.setBounds(area.removeFromTop(24));
    area.removeFromTop(studio::spacingUnit);
}

void Surface::styleField(juce::TextEditor& editor)
{
    editor.setColour(juce::TextEditor::backgroundColourId, theme::background);
    editor.setColour(juce::TextEditor::outlineColourId, theme::line);
    editor.setColour(juce::TextEditor::textColourId, theme::text);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, theme::accent);
    // Text already in the editor carries the colour it was inserted with, so it is repainted here.
    editor.applyColourToAllText(theme::text, true);
}
} // namespace sonicator::dialogs
