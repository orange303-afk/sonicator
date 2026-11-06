#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
// Copies existing files as file references when the platform supports it, otherwise as text paths.
// Returns true only when the clipboard holds file references.
bool copyFilesToClipboard(const juce::Array<juce::File>& files);
} // namespace sonicator
