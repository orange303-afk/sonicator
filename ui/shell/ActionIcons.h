#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator
{
// The single icon set for buttons and menus that this stage touches. Paths are drawn in a unit
// square (0..1) and scaled into the target bounds, so one shape serves every size and the theme.
enum class ActionIcon
{
    settings, search, play, pause, stop, folder, favorite, reset, close, add, remove,
    chevronRight, chevronLeft, chevronDown, more, history
};

juce::Path actionIconPath(ActionIcon icon);

// Draws a stroked icon centred in bounds. Colour is the caller's current colour.
void drawActionIcon(juce::Graphics&, ActionIcon, juce::Rectangle<float> bounds, float strokeWidth = 1.5f);
} // namespace sonicator
