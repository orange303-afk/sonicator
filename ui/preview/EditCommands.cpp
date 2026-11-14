#include "EditCommands.h"
#include "ui/commands/ShortcutBindings.h"

namespace sonicator
{
namespace
{
constexpr int commandFlag = juce::ModifierKeys::commandModifier;
constexpr int shiftFlag = juce::ModifierKeys::shiftModifier;

juce::KeyPress key(int code, int modifiers = 0)
{
    return juce::KeyPress(code, juce::ModifierKeys(modifiers), 0);
}

std::function<ShortcutProfile()>& profileProvider()
{
    static std::function<ShortcutProfile()> provider;
    return provider;
}
}

const std::vector<EditCommandSpec>& editCommandTable()
{
    static const std::vector<EditCommandSpec> table
    {
        { 1, EditAction::undo, CommandContext::waveform, 0, "Undo", key('z', commandFlag) },
        { 2, EditAction::redo, CommandContext::waveform, 0, "Redo",
          key('z', commandFlag | shiftFlag) },
        { 3, EditAction::deleteRegion, CommandContext::waveform, 1, "Delete region",
          key(juce::KeyPress::backspaceKey), key(juce::KeyPress::deleteKey) },
        { 4, EditAction::crop, CommandContext::waveform, 1, "Crop to selection", key('c') },
        { 5, EditAction::silence, CommandContext::waveform, 1, "Silence region", key('s') },
        { 6, EditAction::reverse, CommandContext::waveform, 2, "Reverse", key('r') },
        { 7, EditAction::fadeIn, CommandContext::waveform, 2, "Fade in", key('i') },
        { 8, EditAction::fadeOut, CommandContext::waveform, 2, "Fade out", key('o') },
        { 9, EditAction::normalize, CommandContext::waveform, 2, "Normalize (-0.1 dBFS)",
          key('n') },
        { 10, EditAction::gain, CommandContext::waveform, 2, "Gain...", key('g') },
        { 11, EditAction::toMono, CommandContext::waveform, 2, "Convert to mono", key('m') },
        { 12, EditAction::removeDc, CommandContext::waveform, 2, "Remove DC offset", key('d') },
        { 13, EditAction::trimSilence, CommandContext::waveform, 2, "Trim silence", key('t') },
        { 14, EditAction::zoomToSelection, CommandContext::waveform, 3, "Zoom to selection",
          key('z') },
        { 15, EditAction::zoomToFit, CommandContext::waveform, 3, "Zoom to fit", key('0', commandFlag) },
        { 16, EditAction::save, CommandContext::waveform, 4, "Save...", key('s', commandFlag) },
        // A single letter would rewrite a whole batch by accident, so the library command uses a
        // modified shortcut and belongs to the library context only.
        { 17, EditAction::normalizeSelected, CommandContext::library, 0,
          "Normalize (-0.1 dBFS)...", key('n', commandFlag | shiftFlag) }
    };
    return table;
}

const EditCommandSpec* findEditCommand(EditAction action)
{
    for (const auto& spec : editCommandTable())
        if (spec.action == action) return &spec;
    return nullptr;
}

const EditCommandSpec* findEditCommandById(int id)
{
    for (const auto& spec : editCommandTable())
        if (spec.id == id) return &spec;
    return nullptr;
}

juce::String editCommandId(EditAction action)
{
    switch (action)
    {
        case EditAction::undo: return "waveform.undo";
        case EditAction::redo: return "waveform.redo";
        case EditAction::deleteRegion: return "waveform.deleteRegion";
        case EditAction::crop: return "waveform.crop";
        case EditAction::silence: return "waveform.silence";
        case EditAction::reverse: return "waveform.reverse";
        case EditAction::fadeIn: return "waveform.fadeIn";
        case EditAction::fadeOut: return "waveform.fadeOut";
        case EditAction::normalize: return "waveform.normalize";
        case EditAction::gain: return "waveform.gain";
        case EditAction::toMono: return "waveform.toMono";
        case EditAction::removeDc: return "waveform.removeDc";
        case EditAction::trimSilence: return "waveform.trimSilence";
        case EditAction::zoomToSelection: return "waveform.zoomToSelection";
        case EditAction::zoomToFit: return "waveform.zoomToFit";
        case EditAction::save: return "waveform.save";
        case EditAction::normalizeSelected: return "library.normalizeSelected";
    }
    return {};
}

void setEditCommandProfileProvider(std::function<ShortcutProfile()> provider)
{
    profileProvider() = std::move(provider);
}

juce::String editCommandShortcutText(EditAction action)
{
    const auto* spec = findEditCommand(action);
    if (spec == nullptr) return {};
    // The live binding wins over the table's default, so the menu never advertises a key the
    // router would not run (or hides one the user added).
    if (auto& provider = profileProvider(); provider != nullptr)
    {
        const auto profile = provider();
        std::vector<juce::KeyPress> keys;
        if (profile.hasOverride(editCommandId(action)))
        {
            for (const auto& stored : profile.overrides.at(editCommandId(action)))
                keys.push_back(keyPressFromShortcutKey(stored));
        }
        else
        {
            if (spec->shortcut.isValid()) keys.push_back(spec->shortcut);
            if (spec->alternateShortcut.isValid()) keys.push_back(spec->alternateShortcut);
        }
        return formatShortcuts(keys);
    }
    if (! spec->shortcut.isValid()) return {};
#if JUCE_MAC
    return spec->shortcut.getTextDescriptionWithIcons();
#else
    return spec->shortcut.getTextDescription();
#endif
}

juce::PopupMenu::Item editCommandMenuItem(EditAction action, bool enabled)
{
    juce::PopupMenu::Item item;
    const auto* spec = findEditCommand(action);
    if (spec == nullptr) return item;
    item.itemID = spec->id;
    item.text = spec->label;
    item.isEnabled = enabled;
    item.shortcutKeyDescription = editCommandShortcutText(action);
    return item;
}

bool editCommandMatches(const juce::KeyPress& shortcut, const juce::KeyPress& pressed)
{
    if (! shortcut.isValid() || ! pressed.isValid()) return false;
    const auto required = shortcut.getModifiers();
    const auto actual = pressed.getModifiers();
    // JUCE maps Ctrl to commandModifier on Windows; on macOS Ctrl stays separate, so a stray
    // Ctrl+letter cannot run a Command shortcut.
    if (required.isCommandDown() != actual.isCommandDown()) return false;
    if (required.isShiftDown() != actual.isShiftDown()) return false;
    if (required.isAltDown() != actual.isAltDown()) return false;
    const auto requiredCode = shortcut.getKeyCode();
    const auto actualCode = pressed.getKeyCode();
    if (requiredCode == actualCode) return true;
    const auto lower = [](int code)
    {
        return code >= 32 && code < 127
            ? (int) juce::CharacterFunctions::toLowerCase((juce::juce_wchar) code)
            : code;
    };
    return lower(requiredCode) == lower(actualCode);
}
} // namespace sonicator
