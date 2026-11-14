#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace sonicator
{
// A waveform or library action that has one stable command. The table below is the single source
// for the label, the menu item id and the shortcut: the menus and the key handler both read it, so
// a hint can never outlive or contradict the key that runs it.
enum class EditAction
{
    undo, redo, deleteRegion, crop, silence, reverse, fadeIn, fadeOut,
    normalize, gain, toMono, removeDc, trimSilence, zoomToSelection,
    zoomToFit, save,
    // Library context: normalize the whole selection, never a single letter.
    normalizeSelected
};

// Waveform commands run while the preview owns the keyboard focus. Library commands belong to
// the library table and are handled by MainComponent.
enum class CommandContext { waveform, library };

struct EditCommandSpec
{
    int id = 0;                 // PopupMenu item id, stable and independent of the display order
    EditAction action = EditAction::undo;
    CommandContext context = CommandContext::waveform;
    int group = 0;              // the menu draws a separator between groups
    const char* label = "";
    juce::KeyPress shortcut;
    // A second working key that the menu does not advertise (forward Delete next to Backspace).
    juce::KeyPress alternateShortcut;
};

// Settings -> Keyboard Shortcuts will later read and rewrite this table; nothing else should
// hard-code a label or a key.
const std::vector<EditCommandSpec>& editCommandTable();
const EditCommandSpec* findEditCommand(EditAction);
const EditCommandSpec* findEditCommandById(int id);

// "N", "⌘S", "⇧⌘N" on macOS; "Ctrl + S" elsewhere. Empty when the command has no shortcut.
juce::String editCommandShortcutText(EditAction);

// Menu item with the shortcut in JUCE's right-hand column and the item id from the table.
juce::PopupMenu::Item editCommandMenuItem(EditAction, bool enabled);

// True when the pressed key is exactly the command's shortcut. Letters compare
// case-insensitively and Ctrl counts as Command on macOS, matching the old handlers.
bool editCommandMatches(const juce::KeyPress& shortcut, const juce::KeyPress& pressed);
} // namespace sonicator
