#include <catch2/catch_test_macros.hpp>
#include "ui/preview/EditCommands.h"
#include <array>

using namespace sonicator;

TEST_CASE("Edit command table has stable unique ids and labels", "[commands]")
{
    const auto& table = editCommandTable();
    REQUIRE_FALSE(table.empty());
    for (size_t i = 0; i < table.size(); ++i)
    {
        REQUIRE(table[i].id > 0);
        REQUIRE(juce::String(table[i].label).isNotEmpty());
        for (size_t j = i + 1; j < table.size(); ++j)
            REQUIRE(table[i].id != table[j].id);
    }
    for (const auto& spec : table)
        REQUIRE(findEditCommandById(spec.id) == &spec);
}

TEST_CASE("Every waveform command is reachable by a shortcut", "[commands]")
{
    for (const auto& spec : editCommandTable())
    {
        if (spec.context != CommandContext::waveform) continue;
        INFO("command " << spec.label);
        REQUIRE(spec.shortcut.isValid());
        REQUIRE(editCommandShortcutText(spec.action).isNotEmpty());
    }
    // The library command must never be a single letter: one press would rewrite a batch.
    const auto* library = findEditCommand(EditAction::normalizeSelected);
    REQUIRE(library != nullptr);
    REQUIRE(library->context == CommandContext::library);
    REQUIRE(library->shortcut.getModifiers().isCommandDown());
    REQUIRE(library->shortcut.getModifiers().isShiftDown());
}

TEST_CASE("No two waveform commands share a shortcut", "[commands]")
{
    const auto& table = editCommandTable();
    for (size_t i = 0; i < table.size(); ++i)
    {
        if (table[i].context != CommandContext::waveform) continue;
        const std::array<juce::KeyPress, 2> keys { table[i].shortcut, table[i].alternateShortcut };
        for (size_t j = i + 1; j < table.size(); ++j)
        {
            if (table[j].context != CommandContext::waveform) continue;
            const std::array<juce::KeyPress, 2> other { table[j].shortcut, table[j].alternateShortcut };
            for (const auto& a : keys)
                for (const auto& b : other)
                {
                    if (! a.isValid() || ! b.isValid()) continue;
                    INFO("commands " << table[i].label << " and " << table[j].label);
                    REQUIRE_FALSE(editCommandMatches(a, b));
                }
        }
    }
}

TEST_CASE("Shortcut matcher follows the table and ignores letter case", "[commands]")
{
    const auto* normalize = findEditCommand(EditAction::normalize);
    REQUIRE(normalize != nullptr);
    REQUIRE(editCommandMatches(normalize->shortcut, juce::KeyPress('n')));
    REQUIRE(editCommandMatches(normalize->shortcut, juce::KeyPress('N')));
    REQUIRE_FALSE(editCommandMatches(normalize->shortcut,
        juce::KeyPress('n', juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE_FALSE(editCommandMatches(normalize->shortcut,
        juce::KeyPress('n', juce::ModifierKeys::commandModifier, 0)));

    const auto* undo = findEditCommand(EditAction::undo);
    const auto* redo = findEditCommand(EditAction::redo);
    REQUIRE(undo != nullptr);
    REQUIRE(redo != nullptr);
    const auto commandShift = juce::ModifierKeys(juce::ModifierKeys::commandModifier
                                                 | juce::ModifierKeys::shiftModifier);
    REQUIRE(editCommandMatches(undo->shortcut,
        juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)));
    REQUIRE(editCommandMatches(redo->shortcut, juce::KeyPress('z', commandShift, 0)));
    REQUIRE_FALSE(editCommandMatches(undo->shortcut, juce::KeyPress('z', commandShift, 0)));
    REQUIRE_FALSE(editCommandMatches(undo->shortcut, juce::KeyPress('z')));

    const auto* remove = findEditCommand(EditAction::deleteRegion);
    REQUIRE(remove != nullptr);
    REQUIRE(editCommandMatches(remove->shortcut, juce::KeyPress(juce::KeyPress::backspaceKey)));
    REQUIRE(editCommandMatches(remove->alternateShortcut, juce::KeyPress(juce::KeyPress::deleteKey)));
    REQUIRE_FALSE(editCommandMatches(remove->shortcut, juce::KeyPress('r')));
}

TEST_CASE("Menu item carries the label and the shortcut column", "[commands]")
{
    const auto item = editCommandMenuItem(EditAction::normalize, true);
    REQUIRE(item.itemID == findEditCommand(EditAction::normalize)->id);
    REQUIRE(item.text == "Normalize (-0.1 dBFS)");
    REQUIRE(item.isEnabled);
    REQUIRE(item.shortcutKeyDescription == editCommandShortcutText(EditAction::normalize));
    REQUIRE_FALSE(editCommandMenuItem(EditAction::normalize, false).isEnabled);
}
