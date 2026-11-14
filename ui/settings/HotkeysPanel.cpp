#include "HotkeysPanel.h"
#include "ui/shell/Theme.h"
#include "ui/shell/DesignTokens.h"
#include <algorithm>

namespace sonicator
{
namespace
{
bool usableKey(const juce::KeyPress& key)
{
    if (! key.isValid()) return false;
    // A modifier on its own arrives with no key code; it is not a shortcut.
    if (key.getKeyCode() <= 0) return false;
    return true;
}
} // namespace

HotkeysPanel::HotkeysPanel(const CommandRegistry& commandRegistry)
    : registry(commandRegistry)
{
    setOpaque(false);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(searchBox);
    searchBox.setTextToShowWhenEmpty("Search commands...", theme::muted);
    searchBox.onTextChange = [this] { setSearch(searchBox.getText()); };
    searchBox.setComponentID("hotkeySearch");
    searchBox.setIndents(10, 0);
    // The stock editor draws its text and placeholder at the top; in a 30 px field that reads as
    // a misaligned line. Centre it and give it the interface font, like the library search.
    searchBox.setJustification(juce::Justification::centredLeft);
    searchBox.setFont(studio::controlFont());

    addAndMakeVisible(list);
    list.setModel(this);
    list.setRowHeight(46);
    list.setColour(juce::ListBox::backgroundColourId, theme::background);
    list.setColour(juce::ListBox::outlineColourId, theme::line);
    list.setOutlineThickness(1);

    addAndMakeVisible(currentLabel);
    currentLabel.setFont(studio::captionFont());
    currentLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hintLabel);
    hintLabel.setFont(studio::captionFont());
    hintLabel.setJustificationType(juce::Justification::centredLeft);
    hintLabel.setColour(juce::Label::textColourId, theme::muted);
    hintLabel.setText("Select a command, then add a shortcut. Escape cancels a capture.", juce::dontSendNotification);

    for (auto* button : { &addKey, &removeKey, &resetCommand, &resetEverything, &applyKey, &cancelKey, &replaceKey })
    {
        addAndMakeVisible(button);
        button->setWantsKeyboardFocus(true);
        button->setMouseClickGrabsKeyboardFocus(true);
    }
    addKey.onClick = [this] { beginCapture(); };
    removeKey.onClick = [this] { removeSelectedKeys(); };
    resetCommand.onClick = [this] { resetSelected(); };
    resetEverything.onClick = [this] { resetAll(); };
    applyKey.onClick = [this] { applyCapture(); };
    cancelKey.onClick = [this] { cancelCapture(); };
    replaceKey.onClick = [this] { replaceConflicting(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setFont(studio::captionFont());
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    applyKey.setEnabled(false);
    cancelKey.setEnabled(false);
    replaceKey.setEnabled(false);
    removeKey.setEnabled(false);
    resetCommand.setEnabled(false);
    addKey.setEnabled(false);
    refreshFilter();
}

void HotkeysPanel::setProfile(const ShortcutProfile& profile)
{
    candidate = profile;
    capturing = false;
    pending.clear();
    conflicts.clear();
    applyKey.setEnabled(false);
    cancelKey.setEnabled(false);
    replaceKey.setEnabled(false);
    list.updateContent();
    list.repaint();
    updateFromSelection();
}

void HotkeysPanel::reload()
{
    refreshFilter();
}

void HotkeysPanel::setDiagnostics(const juce::StringArray& messages)
{
    diagnostics = messages;
    resized();
    repaint();
}

void HotkeysPanel::setSearch(const juce::String& text)
{
    if (search == text) return;
    search = text;
    refreshFilter();
}

void HotkeysPanel::refreshFilter()
{
    const auto needle = search.trim();
    auto specs = registry.list();
    std::sort(specs.begin(), specs.end(), [](const CommandSpec& a, const CommandSpec& b)
    {
        if (a.category != b.category) return a.category.compareIgnoreCase(b.category) < 0;
        return a.name.compareIgnoreCase(b.name) < 0;
    });
    specs.erase(std::remove_if(specs.begin(), specs.end(), [&needle](const CommandSpec& spec)
    {
        return needle.isNotEmpty()
            && ! spec.name.containsIgnoreCase(needle)
            && ! spec.description.containsIgnoreCase(needle)
            && ! spec.id.containsIgnoreCase(needle)
            && ! spec.category.containsIgnoreCase(needle);
    }), specs.end());
    filtered = std::move(specs);
    if (selectedRow >= (int) filtered.size()) selectedRow = -1;
    list.updateContent();
    list.repaint();
    updateFromSelection();
}

juce::String HotkeysPanel::rowCommandId(int row) const
{
    return row >= 0 && row < (int) filtered.size() ? filtered[(size_t) row].id : juce::String();
}

const CommandSpec* HotkeysPanel::selectedCommand() const
{
    if (selectedRow < 0 || selectedRow >= (int) filtered.size()) return nullptr;
    return registry.find(filtered[(size_t) selectedRow].id);
}

int HotkeysPanel::getNumRows()
{
    return (int) filtered.size();
}

void HotkeysPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) filtered.size()) return;
    const auto& spec = filtered[(size_t) row];
    if (selected)
    {
        g.setColour(theme::selection);
        g.fillRoundedRectangle(juce::Rectangle<float>(2.0f, 2.0f, (float) width - 4.0f, (float) height - 4.0f), 6.0f);
    }
    auto area = juce::Rectangle<int>(12, 0, juce::jmax(1, width - 24), height);
    g.setColour(theme::text);
    g.setFont(studio::controlFont());
    g.drawFittedText(spec.name, area.removeFromTop(height / 2).withTrimmedTop(2), juce::Justification::centredLeft, 1);
    g.setColour(theme::muted);
    g.setFont(studio::captionFont());
    const auto subtitle = spec.category + "  -  " + spec.id;
    const auto keys = formatShortcuts(effectiveShortcuts(spec, candidate));
    auto keyArea = area.removeFromTop(height / 2).withTrimmedBottom(2);
    const auto keyWidth = keys.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt(studio::controlFont(), keys) + 16;
    g.drawFittedText(subtitle, keyArea.withTrimmedRight(keyWidth), juce::Justification::centredLeft, 1);
    if (keys.isNotEmpty())
    {
        auto chip = keyArea.removeFromRight(keyWidth).reduced(0, 3).toFloat();
        g.setColour(theme::raised);
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(theme::text);
        g.setFont(studio::controlFont());
        g.drawText(keys, chip, juce::Justification::centred, false);
    }
}

void HotkeysPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    selectRow(row);
}

void HotkeysPanel::selectRow(int row)
{
    selectedRow = row;
    list.selectRow(row, true, true);
    updateFromSelection();
}

void HotkeysPanel::updateFromSelection()
{
    const auto* spec = selectedCommand();
    const bool has = spec != nullptr;
    addKey.setEnabled(has && ! capturing);
    removeKey.setEnabled(has && ! effectiveShortcuts(*spec, candidate).empty());
    resetCommand.setEnabled(has && candidate.hasOverride(spec->id));
    currentLabel.setText(has ? spec->name + "  -  " + spec->category : "Select a command",
                         juce::dontSendNotification);
    currentLabel.setColour(juce::Label::textColourId, has ? theme::text : theme::muted);
    repaint();
}

void HotkeysPanel::beginCapture()
{
    if (selectedCommand() == nullptr) return;
    capturing = true;
    pending.clear();
    conflicts.clear();
    status = "Press the key combination. Escape cancels.";
    applyKey.setEnabled(false);
    cancelKey.setEnabled(true);
    replaceKey.setEnabled(false);
    addKey.setEnabled(false);
    // The panel itself takes the keyboard: JUCE's ListBox swallows Delete, Return and the arrow
    // keys, and those are exactly the keys a user may want to bind.
    if (isShowing()) grabKeyboardFocus();
    resized();
    repaint();
}

bool HotkeysPanel::captureKey(const juce::KeyPress& key)
{
    if (! capturing) return false;
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        cancelCapture();
        return true;
    }
    if (! usableKey(key)) return true; // swallow modifier-only presses while capturing
    pending.push_back(key);
    capturing = false;
    status = "New shortcut: " + formatShortcut(key) + ". Apply to keep it.";
    applyKey.setEnabled(true);
    cancelKey.setEnabled(true);
    resized();
    repaint();
    return true;
}

void HotkeysPanel::cancelCapture()
{
    capturing = false;
    pending.clear();
    conflicts.clear();
    stagedCommand.clear();
    stagedOverride.clear();
    status.clear();
    applyKey.setEnabled(false);
    cancelKey.setEnabled(false);
    replaceKey.setEnabled(false);
    updateFromSelection();
    resized();
    repaint();
}

void HotkeysPanel::applyCapture()
{
    const auto* spec = selectedCommand();
    if (spec == nullptr || pending.empty()) return;
    auto existing = effectiveShortcuts(*spec, candidate);
    for (const auto& key : pending)
        existing.push_back(key);
    // Deduplicate identical keys.
    std::vector<juce::KeyPress> unique;
    for (const auto& key : existing)
    {
        bool duplicate = false;
        for (const auto& kept : unique)
            if (shortcutMatches(kept, key)) { duplicate = true; break; }
        if (! duplicate) unique.push_back(key);
    }
    const auto conflictsFound = conflictingCommands(registry, candidate, spec->id, pending);
    if (! conflictsFound.empty())
    {
        // Stage the keys but do not publish them: the clash must be resolved first, and a
        // cancelled conflict must leave the candidate exactly as it was.
        stagedCommand = spec->id;
        stagedOverride = storedKeysFromKeyPresses(unique);
        showConflictFor(pending);
        return;
    }
    candidate.overrides[spec->id] = storedKeysFromKeyPresses(unique);
    pending.clear();
    stagedCommand.clear();
    stagedOverride.clear();
    applyKey.setEnabled(false);
    cancelKey.setEnabled(false);
    status = "Saved.";
    if (! commit())
        status = "Could not save the shortcut. The previous profile is still in effect.";
    list.updateContent();
    list.repaint();
    updateFromSelection();
}

void HotkeysPanel::showConflictFor(const std::vector<juce::KeyPress>&)
{
    juce::StringArray lines;
    const auto id = selectedCommand() != nullptr ? selectedCommand()->id : juce::String();
    for (const auto& other : conflictingCommands(registry, candidate, id, pending))
        if (const auto* spec = registry.find(other))
            lines.add(spec->name + " (" + spec->category + ")");
    if (lines.size() == 0)
    {
        conflicts.clear();
        replaceKey.setEnabled(false);
        return;
    }
    conflicts = "Already used by " + lines.joinIntoString(", ") + ". Replace that binding?";
    status = conflicts;
    replaceKey.setEnabled(true);
    cancelKey.setEnabled(true);
    resized();
    repaint();
}

void HotkeysPanel::replaceConflicting()
{
    if (stagedCommand.isEmpty() || pending.empty()) return;
    for (const auto& other : conflictingCommands(registry, candidate, stagedCommand, pending))
    {
        auto keys = effectiveShortcuts(*registry.find(other), candidate);
        std::vector<juce::KeyPress> kept;
        for (const auto& key : keys)
        {
            bool clashes = false;
            for (const auto& candidateKey : pending)
                if (shortcutMatches(key, candidateKey)) { clashes = true; break; }
            if (! clashes) kept.push_back(key);
        }
        candidate.overrides[other] = storedKeysFromKeyPresses(kept);
    }
    candidate.overrides[stagedCommand] = stagedOverride;
    conflicts.clear();
    pending.clear();
    stagedCommand.clear();
    stagedOverride.clear();
    replaceKey.setEnabled(false);
    applyKey.setEnabled(false);
    cancelKey.setEnabled(false);
    status = "Saved.";
    if (! commit())
        status = "Could not save the shortcut. The previous profile is still in effect.";
    list.updateContent();
    list.repaint();
    updateFromSelection();
}

void HotkeysPanel::removeSelectedKeys()
{
    const auto* spec = selectedCommand();
    if (spec == nullptr) return;
    // An explicit empty override removes every key; the command stays usable from its menu.
    candidate.overrides[spec->id] = {};
    status = "Shortcut removed.";
    if (! commit())
        status = "Could not save the change.";
    list.updateContent();
    list.repaint();
    updateFromSelection();
}

void HotkeysPanel::resetSelected()
{
    const auto* spec = selectedCommand();
    if (spec == nullptr) return;
    candidate.overrides.erase(spec->id);
    status = "Default shortcut restored.";
    if (! commit())
        status = "Could not save the change.";
    list.updateContent();
    list.repaint();
    updateFromSelection();
}

void HotkeysPanel::resetAll()
{
    if (onConfirmResetAll != nullptr)
    {
        onConfirmResetAll([safe = juce::Component::SafePointer<HotkeysPanel>(this)](bool confirmed)
        {
            if (safe != nullptr && confirmed) safe->applyResetAll();
        });
        return;
    }
    applyResetAll();
}

void HotkeysPanel::applyResetAll()
{
    candidate.overrides.clear();
    capturing = false;
    pending.clear();
    conflicts.clear();
    status = "Every command uses its default shortcut.";
    if (! commit())
        status = "Could not save the change.";
    list.updateContent();
    list.repaint();
    updateFromSelection();
}

bool HotkeysPanel::commit()
{
    if (onProfileChanged == nullptr) return true;
    return onProfileChanged(candidate);
}

bool HotkeysPanel::keyPressed(const juce::KeyPress& key)
{
    if (capturing) return captureKey(key);
    // Space would normally press a focused button; the list owns the keyboard instead.
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused != nullptr && dynamic_cast<const juce::Button*>(focused) != nullptr)
        return false;
    return false;
}

void HotkeysPanel::resized()
{
    auto bounds = getLocalBounds();
    searchBox.setBounds(bounds.removeFromTop(studio::controlHeight));
    bounds.removeFromTop(studio::spacing);
    auto footer = bounds.removeFromBottom(capturing ? 76 : 96);
    list.setBounds(bounds);
    footer.removeFromTop(studio::spacingTight);
    if (capturing)
    {
        auto row = footer.removeFromTop(studio::controlHeight);
        currentLabel.setBounds(row);
        footer.removeFromTop(studio::spacingTight);
        auto buttons = footer.removeFromTop(studio::controlHeight);
        cancelKey.setBounds(buttons.removeFromLeft(96));
        hintLabel.setBounds(buttons.removeFromLeft(buttons.getWidth()));
        hintLabel.setVisible(true);   // the capture hint owns this row on its own
        return;
    }
    // Both footer rows share one four-column grid, so the buttons line up in columns instead of
    // ending at four different edges.
    const int columnGap = 6;
    auto capture = footer.removeFromTop(studio::controlHeight);
    const int columnWidth = (capture.getWidth() - 3 * columnGap) / 4;
    auto column = [&capture, columnWidth, columnGap](int index)
    {
        return juce::Rectangle<int>(capture.getX() + index * (columnWidth + columnGap), capture.getY(),
                                    columnWidth, capture.getHeight());
    };
    currentLabel.setBounds(column(0));
    replaceKey.setBounds(column(1));
    applyKey.setBounds(column(2));
    cancelKey.setBounds(column(3));
    footer.removeFromTop(studio::spacingTight);
    addKey.setBounds(column(0).withY(footer.getY()));
    removeKey.setBounds(column(1).withY(footer.getY()));
    resetCommand.setBounds(column(2).withY(footer.getY()));
    resetEverything.setBounds(column(3).withY(footer.getY()));
    footer.removeFromTop(studio::controlHeight);
    // The grid fills that row, so the reminder moves to the status line instead of overlapping it.
    hintLabel.setVisible(false);
    hintLabel.setBounds({});
    footer.removeFromTop(4);
    const bool hasDiagnostics = ! diagnostics.isEmpty();
    const int diagnosticsHeight = hasDiagnostics ? (int) juce::jmin(44, 16 * diagnostics.size()) : 0;
    if (hasDiagnostics) statusLabel.setBounds(footer.removeFromBottom(diagnosticsHeight));
    else statusLabel.setBounds(footer.removeFromTop(18));
    const auto hint = juce::String("Select a command, then add a shortcut. Escape cancels a capture.");
    statusLabel.setText(hasDiagnostics ? diagnostics.joinIntoString("  ")
                                       : (conflicts.isNotEmpty() ? conflicts
                                                                 : (status.isNotEmpty() ? status : hint)),
                        juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId,
        hasDiagnostics || conflicts.isNotEmpty() ? theme::accent : theme::muted);
}

void HotkeysPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
}

void HotkeysPanel::lookAndFeelChanged()
{
    searchBox.setColour(juce::TextEditor::backgroundColourId, theme::panel);
    searchBox.setColour(juce::TextEditor::textColourId, theme::text);
    hintLabel.setColour(juce::Label::textColourId, conflicts.isNotEmpty() ? theme::accent : theme::muted);
    // Four buttons leave little room on this row; a hint cut off with an ellipsis reads worse than
    // no hint, so it only shows when it fits. A conflict message still takes the row - it matters.
    hintLabel.setVisible(conflicts.isNotEmpty()
        || hintLabel.getWidth() >= juce::GlyphArrangement::getStringWidthInt(hintLabel.getFont(),
                                                                            hintLabel.getText()));
    statusLabel.setColour(juce::Label::textColourId, theme::muted);
    currentLabel.setColour(juce::Label::textColourId, selectedCommand() != nullptr ? theme::text : theme::muted);
    list.setColour(juce::ListBox::backgroundColourId, theme::background);
    list.setColour(juce::ListBox::outlineColourId, theme::line);
    repaint();
}

void HotkeysPanel::clearStatus()
{
    status.clear();
    conflicts.clear();
}
} // namespace sonicator
