#pragma once
#include "ui/commands/CommandRegistry.h"
#include "ui/commands/ShortcutBindings.h"
#include "core/settings/ShortcutPreferences.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace sonicator
{
// The Hotkeys page. It works on a candidate profile and hands the whole profile to its owner,
// which persists it and only then publishes it to the router: a failed write keeps the old keys
// in effect, and the form keeps the user's entry.
class HotkeysPanel final : public juce::Component,
                           private juce::ListBoxModel
{
public:
    explicit HotkeysPanel(const CommandRegistry&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress&) override;

    // Replaces the profile being edited (for example after another window changed it).
    void setProfile(const ShortcutProfile&);
    // Configuration problems (a conflicting override that was not applied).
    void setDiagnostics(const juce::StringArray&);
    const ShortcutProfile& profile() const { return candidate; }
    // Sends the candidate to the owner. A rejected write leaves the candidate on screen so
    // nothing the user typed disappears; the previously published profile stays in effect.
    bool commit();

    void setSearch(const juce::String&);
    // Re-reads the registry, for commands registered after the panel was created.
    void reload();
    int commandRows() const { return (int) filtered.size(); }
    juce::String rowCommandId(int row) const;
    void selectCommandRow(int row) { selectRow(row); }

    // Capture: the next key becomes the first candidate key. Escape cancels.
    void beginCapture();
    bool isCapturing() const { return capturing; }
    // Returns true when the key was consumed by the capture. Modifier-only presses are ignored.
    bool captureKey(const juce::KeyPress&);
    // The keys staged for the selected command (0 or 1 during capture, then edited by Apply).
    std::vector<juce::KeyPress> pendingKeys() const { return pending; }
    void applyCapture();
    void cancelCapture();

    void removeSelectedKeys();
    void resetSelected();
    // Resets every command to its defaults after onConfirmResetAll returns true.
    void resetAll();

    juce::String conflictText() const { return conflicts; }
    juce::String statusText() const { return status; }
    juce::String diagnosticsText() const { return diagnostics.joinIntoString("\n"); }
    // Resolves the pending conflict by replacing the other commands' keys.
    void replaceConflicting();

    std::function<bool(const ShortcutProfile&)> onProfileChanged; // false rejects the write
    // Asks the owner to confirm; the callback receives the answer and only then resets.
    std::function<void(std::function<void(bool)>)> onConfirmResetAll;

    enum ColourIds { panelBackgroundColourId = 0x2000100 };

private:
    void refreshFilter();
    void selectRow(int row);
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void updateFromSelection();
    void applyResetAll();
    const CommandSpec* selectedCommand() const;
    void showConflictFor(const std::vector<juce::KeyPress>&);
    void clearStatus();

    const CommandRegistry& registry;
    ShortcutProfile candidate;
    std::vector<CommandSpec> filtered;
    juce::String search;
    juce::String status, conflicts;
    juce::StringArray diagnostics;
    bool capturing = false;
    std::vector<juce::KeyPress> pending;
    juce::String stagedCommand;
    std::vector<ShortcutKey> stagedOverride;
    int selectedRow = -1;

    juce::TextEditor searchBox;
    juce::ListBox list;
    juce::TextButton addKey { "Add shortcut" }, removeKey { "Remove" },
                     resetCommand { "Reset command" }, resetEverything { "Reset all" },
                     applyKey { "Apply" }, cancelKey { "Cancel" }, replaceKey { "Replace" };
    juce::Label currentLabel, hintLabel, statusLabel;
    juce::Rectangle<int> captureBounds;
};
} // namespace sonicator
