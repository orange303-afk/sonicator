#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
namespace theme {
inline const juce::Colour background { 0xff0c1018 }, panel { 0xff141923 }, raised { 0xff1c2230 };
inline const juce::Colour text { 0xffe9e8f2 }, muted { 0xff939caf }, accent { 0xffb38aff }, line { 0xff2a3040 };
}
class SonicatorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SonicatorLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId,theme::background);
        setColour(juce::TextButton::buttonColourId,theme::raised);
        setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff533b79));
        setColour(juce::TextButton::textColourOffId,theme::text);
        setColour(juce::TextButton::textColourOnId,theme::text);
        setColour(juce::Label::textColourId,theme::text);
        setColour(juce::TextEditor::backgroundColourId,theme::panel);
        setColour(juce::TextEditor::textColourId,theme::text);
        setColour(juce::TextEditor::outlineColourId,theme::line);
        setColour(juce::TextEditor::focusedOutlineColourId,theme::accent);
        setColour(juce::Slider::thumbColourId,theme::accent);
        setColour(juce::Slider::trackColourId,theme::accent);
        setColour(juce::ToggleButton::textColourId,theme::text);
        setColour(juce::ToggleButton::tickColourId,theme::accent);
        setColour(juce::TableHeaderComponent::backgroundColourId,theme::raised);
        setColour(juce::TableHeaderComponent::textColourId,theme::muted);
        setColour(juce::TableHeaderComponent::outlineColourId,theme::line);
    }
};
}
