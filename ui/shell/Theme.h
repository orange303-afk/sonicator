#pragma once
#include "core/settings/AppSettings.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator::theme {
inline juce::Colour background { 0xff0c1018 }, panel { 0xff141923 }, raised { 0xff1c2230 };
inline juce::Colour text { 0xffe9e8f2 }, muted { 0xff939caf }, accent { 0xffb38aff };
inline juce::Colour line { 0xff2a3040 }, selection { 0xff342844 };
inline bool isDark = true;

inline bool systemPrefersDark()
{
    return juce::Desktop::getInstance().isDarkModeActive();
}
inline void apply(ThemeMode mode)
{
    isDark = mode == ThemeMode::Dark || (mode == ThemeMode::System && systemPrefersDark());
    if (isDark)
    {
        background = juce::Colour(0xff121110); panel = juce::Colour(0xff1a1815); raised = juce::Colour(0xff26221e);
        text = juce::Colour(0xfff0eae2); muted = juce::Colour(0xffa2988c); accent = juce::Colour(0xffe8873c);
        line = juce::Colour(0xff3a332c); selection = juce::Colour(0xff3d2a1b);
    }
    else
    {
        background = juce::Colour(0xfff5f2ee); panel = juce::Colour(0xffffffff); raised = juce::Colour(0xffeae4dc);
        text = juce::Colour(0xff211d19); muted = juce::Colour(0xff6d6358); accent = juce::Colour(0xffc2621f);
        line = juce::Colour(0xffddd5cb); selection = juce::Colour(0xfff4e3d3);
    }
}
// Tag colours are chosen for dots and borders; their label needs extra contrast on light panels.
inline juce::Colour tagText(juce::Colour tagColour)
{
    return isDark ? tagColour.brighter(0.55f) : tagColour.darker(0.42f);
}
}
namespace sonicator {
class SonicatorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SonicatorLookAndFeel() { refreshColours(); }
    void refreshColours()
    {
        setColour(juce::ResizableWindow::backgroundColourId,theme::background);
        setColour(juce::TextButton::buttonColourId,theme::raised);
        setColour(juce::TextButton::buttonOnColourId,theme::accent.withAlpha(theme::isDark ? 0.45f : 0.22f));
        setColour(juce::TextButton::textColourOffId,theme::text);
        setColour(juce::TextButton::textColourOnId,theme::text);
        setColour(juce::Label::textColourId,theme::text);
        setColour(juce::TextEditor::backgroundColourId,theme::panel);
        setColour(juce::TextEditor::textColourId,theme::text);
        setColour(juce::TextEditor::outlineColourId,theme::line);
        setColour(juce::TextEditor::focusedOutlineColourId,theme::accent);
        setColour(juce::TextEditor::highlightColourId,theme::accent.withAlpha(0.25f));
        setColour(juce::TextEditor::highlightedTextColourId,theme::text);
        setColour(juce::Slider::thumbColourId,theme::accent);
        setColour(juce::Slider::trackColourId,theme::accent);
        setColour(juce::Slider::backgroundColourId,theme::line);
        setColour(juce::ToggleButton::textColourId,theme::text);
        setColour(juce::ToggleButton::tickColourId,theme::accent);
        setColour(juce::TableHeaderComponent::backgroundColourId,theme::raised);
        setColour(juce::TableHeaderComponent::textColourId,theme::muted);
        setColour(juce::TableHeaderComponent::outlineColourId,theme::line);
        setColour(juce::ListBox::backgroundColourId,theme::background);
        setColour(juce::ListBox::outlineColourId,theme::line);
        setColour(juce::ListBox::textColourId,theme::text);
        setColour(juce::PopupMenu::backgroundColourId,theme::panel);
        setColour(juce::PopupMenu::textColourId,theme::text);
        setColour(juce::PopupMenu::headerTextColourId,theme::muted);
        setColour(juce::PopupMenu::highlightedBackgroundColourId,theme::selection);
        setColour(juce::PopupMenu::highlightedTextColourId,theme::text);
        setColour(juce::ComboBox::backgroundColourId,theme::panel);
        setColour(juce::ComboBox::textColourId,theme::text);
        setColour(juce::ComboBox::outlineColourId,theme::line);
        setColour(juce::ComboBox::arrowColourId,theme::muted);
        setColour(juce::ComboBox::focusedOutlineColourId,theme::accent);
        setColour(juce::AlertWindow::backgroundColourId,theme::panel);
        setColour(juce::AlertWindow::textColourId,theme::text);
        setColour(juce::AlertWindow::outlineColourId,theme::line);
        setColour(juce::ScrollBar::thumbColourId,theme::muted.withAlpha(0.6f));
        setColour(juce::ScrollBar::trackColourId,theme::background);
        setColour(juce::HyperlinkButton::textColourId,theme::accent);
    }
};
} // namespace sonicator
