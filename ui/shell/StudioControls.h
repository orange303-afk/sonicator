#pragma once
#include "ActionIcons.h"
#include "DesignTokens.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator::studio
{
// The Studio surfaces are properties on ordinary JUCE buttons rather than new control classes:
// the LookAndFeel paints them, while the button keeps its text (the accessible name and what the
// tests look up), its toggle state and its callbacks.
namespace properties
{
inline constexpr const char* icon = "studioIcon";
inline constexpr const char* trailingIcon = "studioTrailingIcon";
inline constexpr const char* circle = "studioCircle";
inline constexpr const char* chip = "studioChip";
inline constexpr const char* checkChip = "studioCheckChip";
} // namespace properties

// -1 means "this control has no icon", so ActionIcon::settings (0) is never mistaken for a default.
inline int iconProperty(const juce::Button& button)
{
    return (int) button.getProperties().getWithDefault(properties::icon, -1);
}
inline void setIcon(juce::Button& button, ActionIcon icon)
{
    button.getProperties().set(properties::icon, (int) icon);
    button.repaint();
}
inline void setCircle(juce::Button& button, bool round)
{
    button.getProperties().set(properties::circle, round);
    button.repaint();
}
// 1 is the 30 px chip, 2 the 26 px one, 0 is not a chip.
inline int chipStyle(const juce::Button& button)
{
    return (int) button.getProperties().getWithDefault(properties::chip, 0);
}
inline bool isCheckChip(const juce::Button& button)
{
    return (int) button.getProperties().getWithDefault(properties::checkChip, 0) > 0;
}

// A square icon button. The text stays for accessibility and for the tests that find buttons by
// their text; the LookAndFeel draws the icon and no text.
class IconButton final : public juce::TextButton
{
public:
    IconButton(ActionIcon icon, const juce::String& accessibleName);
    ActionIcon icon() const noexcept;
    void setIcon(ActionIcon);
};

// A filter pill. Toggle state is driven by the owner (the library filter), exactly as the toolbar
// buttons drive it today, so a chip never toggles itself out of step with the filter.
class Chip final : public juce::TextButton
{
public:
    explicit Chip(const juce::String& text, bool small = false);
    void setTrailingIcon(ActionIcon);
};

// A pill that keeps the ToggleButton's tick, so the state reads without relying on fill alone.
class CheckChip final : public juce::ToggleButton
{
public:
    explicit CheckChip(const juce::String& text, bool small = false);
};

// One value and one caption, the Analysis card's unit.
struct Stat
{
    juce::String value, caption;
};
class StatTile final : public juce::Component
{
public:
    void setStat(const Stat&);
    const Stat& stat() const noexcept { return data; }
    static int preferredHeight();
    void paint(juce::Graphics&) override;
private:
    Stat data;
};

// The small upper-case group heading above a set of controls.
class SectionCaption final : public juce::Label
{
public:
    explicit SectionCaption(const juce::String& text);
};
} // namespace sonicator::studio
