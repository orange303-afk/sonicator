#pragma once
#include "AppFonts.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator::studio
{
// The Studio layout scale. Every new control in Settings, Hotkeys and the panel dividers measures
// itself from here instead of picking its own numbers, so the screens stay consistent at 100-150%
// interface scale. Values are logical pixels: JUCE applies the interface scale afterwards.
inline constexpr int spacingUnit = 4;
inline constexpr int spacingTight = spacingUnit * 2;      // 8
inline constexpr int spacing = spacingUnit * 3;           // 12
inline constexpr int spacingWide = spacingUnit * 4;       // 16
inline constexpr int spacingSection = spacingUnit * 6;    // 24

inline constexpr float controlRadius = 6.0f;
inline constexpr float panelRadius = 10.0f;
inline constexpr float dialogRadius = 12.0f;

inline constexpr int controlHeight = 30;
inline constexpr int navigationWidth = 172;
inline constexpr int dividerThickness = 8;

// The workspace scale: the same 4 px step as Settings, with the sizes this window needs.
inline constexpr int iconButtonSize = 32;
inline constexpr int circleButtonSize = 36;
inline constexpr int chipHeight = 30;
inline constexpr int chipSmallHeight = 26;
inline constexpr int chipRadius = 15;
// A short label ("Tags") still has to read as a pill, not a dot.
inline constexpr int chipMinWidth = 64;
inline constexpr int headerHeight = 72;
inline constexpr int brandingWidth = 298;
inline constexpr int tableRowHeight = 36;

// Manrope carries all four weights; a heavier face is asked for by name, not synthesized.
inline juce::Font bodyFont()
{
    return fonts::font(14.0f);
}
inline juce::Font controlFont()
{
    return fonts::font(13.0f);
}
inline juce::Font captionFont()
{
    return fonts::font(12.0f);
}
// Small uppercase group caption, the "APPEARANCE" style heading.
inline juce::Font sectionFont()
{
    return fonts::font(10.5f, fonts::Weight::semiBold).withExtraKerningFactor(0.12f);
}
inline juce::Font pageTitleFont()
{
    return fonts::font(24.0f, fonts::Weight::semiBold);
}
inline juce::Font pageSubtitleFont()
{
    return fonts::font(13.0f);
}
// The larger line of a value + caption pair (the Analysis card).
inline juce::Font statValueFont()
{
    return fonts::font(17.0f, fonts::Weight::semiBold);
}
inline juce::Font statCaptionFont()
{
    return captionFont();
}
// The large number of the Analysis hero block (Key, BPM).
inline juce::Font heroValueFont()
{
    return fonts::font(26.0f, fonts::Weight::semiBold);
}
// Table column headers: the section size, but without the tracking that makes "FORMAT" wider
// than its minimum column.
inline juce::Font tableHeaderFont()
{
    return fonts::font(10.5f, fonts::Weight::semiBold);
}
} // namespace sonicator::studio
