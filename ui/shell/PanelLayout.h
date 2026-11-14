#pragma once
#include "core/settings/AppSettings.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator
{
// The three adjustable panels plus the existing Favorites divider. Preferred sizes come from the
// settings file; effective sizes are what this window can show right now. The two are separate so
// shrinking a window never destroys the user's preference.
struct PanelLayoutBounds
{
    juce::Rectangle<int> folders, library, analysis, preview;
    juce::Rectangle<int> foldersDivider, analysisDivider, previewDivider;
    bool compact = false;
};

inline constexpr int minFoldersWidth = 160;
inline constexpr int minAnalysisWidth = 200;
inline constexpr int minLibraryWidth = 320;
inline constexpr int minPreviewHeight = 156;
inline constexpr int minLibraryHeight = 180;
inline constexpr int panelDividerThickness = 8;

// The adaptive defaults that the fixed 190/240 and 230/270 rule produced, expressed as a
// preference so "Reset layout" restores them.
PanelLayoutPreferences defaultPanelLayout(int windowWidth);

// Fills every rectangle inside `workspace`. Panels never overlap, never get a negative size, and
// dividers keep their thickness whenever the workspace allows it.
PanelLayoutBounds computePanelLayout(juce::Rectangle<int> workspace, const PanelLayoutPreferences&);
} // namespace sonicator
