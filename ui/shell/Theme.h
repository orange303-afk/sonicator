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
        background = juce::Colour(0xff0c1018); panel = juce::Colour(0xff141923); raised = juce::Colour(0xff1c2230);
        text = juce::Colour(0xffe9e8f2); muted = juce::Colour(0xff939caf); accent = juce::Colour(0xffb38aff);
        line = juce::Colour(0xff2a3040); selection = juce::Colour(0xff342844);
    }
    else
    {
        background = juce::Colour(0xfff4f5f8); panel = juce::Colour(0xffffffff); raised = juce::Colour(0xffe9ecf3);
        text = juce::Colour(0xff1c2028); muted = juce::Colour(0xff606979); accent = juce::Colour(0xff6b4fd8);
        line = juce::Colour(0xffd7dbe4); selection = juce::Colour(0xffe8e2fb);
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
