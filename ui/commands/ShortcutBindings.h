#pragma once
#include "CommandRegistry.h"
#include "core/settings/ShortcutPreferences.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace sonicator
{
ShortcutKey shortcutKeyFromKeyPress(const juce::KeyPress&);
juce::KeyPress keyPressFromShortcutKey(const ShortcutKey&);
std::vector<ShortcutKey> storedKeysFromKeyPresses(const std::vector<juce::KeyPress>&);

// True when the pressed key is exactly the shortcut: same key code (letters compare
// case-insensitively) and the same modifiers. Command and Control are compared separately on
// macOS so a stray Ctrl+letter never fires a Command shortcut.
bool shortcutMatches(const juce::KeyPress& shortcut, const juce::KeyPress& pressed);

// The user's overrides when present, otherwise the command's defaults. An override with an empty
// list is a deliberate "no key".
std::vector<juce::KeyPress> effectiveShortcuts(const CommandSpec&, const ShortcutProfile&);
bool hasOverride(const ShortcutProfile&, const juce::String& id);

// "N", "⌘S", "⇧⌘N" on macOS; "Ctrl + S" elsewhere. Empty when there is no key.
juce::String formatShortcut(const juce::KeyPress&);
juce::String formatShortcuts(const std::vector<juce::KeyPress>&);

// Two scopes overlap when one key can reach both; only then may the same key mean two things.
bool scopesOverlap(CommandScope a, CommandScope b);

// Commands whose effective bindings use one of `candidateKeys` in a scope that overlaps `id`'s.
std::vector<juce::String> conflictingCommands(const CommandRegistry&, const ShortcutProfile&,
                                             const juce::String& id,
                                             const std::vector<juce::KeyPress>& candidateKeys);

// Drops conflicting user bindings (and later conflicting defaults) so one key never runs two
// commands. The stored profile is left untouched; this returns the profile to execute.
ShortcutProfile sanitizedProfile(const CommandRegistry&, const ShortcutProfile&,
                                juce::StringArray* diagnostics = nullptr);
} // namespace sonicator
