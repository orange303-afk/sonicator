#pragma once
#include "core/settings/AppSettings.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
// Chooses a visible, usable window rectangle. Off-screen or degenerate saved bounds fall back to
// a centred default; sizes are clamped to the minimum and to the current display work area.
juce::Rectangle<int> resolveWindowBounds(juce::Rectangle<int> saved,
                                         juce::Rectangle<int> available,
                                         int minWidth = 880,
                                         int minHeight = 580);
juce::Rectangle<int> resolveWindowBounds(const AppSettings&, const juce::Rectangle<int>& available);
} // namespace sonicator
