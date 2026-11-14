#include "ShortcutBindings.h"
#include <algorithm>

namespace sonicator
{
namespace
{
int lowerIfAlpha(int code)
{
    return code >= 32 && code < 127
        ? (int) juce::CharacterFunctions::toLowerCase((juce::juce_wchar) code)
        : code;
}

juce::String describeKey(const juce::KeyPress& key)
{
    return formatShortcut(key);
}

bool sameKey(const juce::KeyPress& a, const juce::KeyPress& b)
{
    return shortcutMatches(a, b);
}
} // namespace

ShortcutKey shortcutKeyFromKeyPress(const juce::KeyPress& key)
{
    ShortcutKey stored;
    if (! key.isValid()) return stored;
    stored.keyCode = key.getKeyCode();
    stored.command = key.getModifiers().isCommandDown();
    stored.control = key.getModifiers().isCtrlDown();
    stored.alt = key.getModifiers().isAltDown();
    stored.shift = key.getModifiers().isShiftDown();
    return stored;
}

juce::KeyPress keyPressFromShortcutKey(const ShortcutKey& key)
{
    if (! key.isValid()) return {};
    int modifiers = 0;
    if (key.command) modifiers |= juce::ModifierKeys::commandModifier;
    if (key.control) modifiers |= juce::ModifierKeys::ctrlModifier;
    if (key.alt) modifiers |= juce::ModifierKeys::altModifier;
    if (key.shift) modifiers |= juce::ModifierKeys::shiftModifier;
    return juce::KeyPress(key.keyCode, juce::ModifierKeys(modifiers), 0);
}

std::vector<ShortcutKey> storedKeysFromKeyPresses(const std::vector<juce::KeyPress>& keys)
{
    std::vector<ShortcutKey> stored;
    stored.reserve(keys.size());
    for (const auto& key : keys)
        if (key.isValid()) stored.push_back(shortcutKeyFromKeyPress(key));
    return stored;
}

bool shortcutMatches(const juce::KeyPress& shortcut, const juce::KeyPress& pressed)
{
    if (! shortcut.isValid() || ! pressed.isValid()) return false;
    const auto required = shortcut.getModifiers();
    const auto actual = pressed.getModifiers();
    if (required.isCommandDown() != actual.isCommandDown()) return false;
    if (required.isShiftDown() != actual.isShiftDown()) return false;
    if (required.isAltDown() != actual.isAltDown()) return false;
    // On Windows JUCE reports Ctrl as Command; on macOS the two are distinct.
    if (required.isCtrlDown() != actual.isCtrlDown()) return false;
    // Extra flags (mouse buttons) must not decide a match. The explicit modifier tests above
    // already cover Command, Shift, Alt and Control on both platforms.
    return lowerIfAlpha(shortcut.getKeyCode()) == lowerIfAlpha(pressed.getKeyCode());
}

bool hasOverride(const ShortcutProfile& profile, const juce::String& id)
{
    return profile.hasOverride(id);
}

std::vector<juce::KeyPress> effectiveShortcuts(const CommandSpec& command, const ShortcutProfile& profile)
{
    const auto it = profile.overrides.find(command.id);
    if (it == profile.overrides.end()) return command.defaults;
    std::vector<juce::KeyPress> keys;
    keys.reserve(it->second.size());
    for (const auto& stored : it->second)
    {
        const auto key = keyPressFromShortcutKey(stored);
        if (key.isValid()) keys.push_back(key);
    }
    return keys;
}

juce::String formatShortcut(const juce::KeyPress& key)
{
    if (! key.isValid()) return {};
#if JUCE_MAC
    return key.getTextDescriptionWithIcons();
#else
    return key.getTextDescription();
#endif
}

juce::String formatShortcuts(const std::vector<juce::KeyPress>& keys)
{
    juce::StringArray parts;
    for (const auto& key : keys)
    {
        const auto text = formatShortcut(key);
        if (text.isNotEmpty()) parts.add(text);
    }
    return parts.joinIntoString(", ");
}

bool scopesOverlap(CommandScope a, CommandScope b)
{
    if (a == b) return true;
    // An application command is reachable whenever its panel is focused, so it overlaps every
    // panel scope. Panel scopes are mutually exclusive: only one owns the keyboard at a time.
    return a == CommandScope::app || b == CommandScope::app;
}

std::vector<juce::String> conflictingCommands(const CommandRegistry& registry,
                                             const ShortcutProfile& profile,
                                             const juce::String& id,
                                             const std::vector<juce::KeyPress>& candidateKeys)
{
    std::vector<juce::String> conflicts;
    const auto* target = registry.find(id);
    if (target == nullptr) return conflicts;
    for (const auto& command : registry.list())
    {
        if (command.id == id) continue;
        if (! scopesOverlap(target->scope, command.scope)) continue;
        for (const auto& candidate : candidateKeys)
        {
            if (! candidate.isValid()) continue;
            for (const auto& existing : effectiveShortcuts(command, profile))
            {
                if (! sameKey(existing, candidate)) continue;
                if (std::find(conflicts.begin(), conflicts.end(), command.id) == conflicts.end())
                    conflicts.push_back(command.id);
                break;
            }
        }
    }
    return conflicts;
}

ShortcutProfile sanitizedProfile(const CommandRegistry& registry, const ShortcutProfile& profile,
                                juce::StringArray* diagnostics)
{
    ShortcutProfile result;
    result.schemaVersion = profile.schemaVersion;
    result.readable = profile.readable;
    if (! profile.readable)
    {
        // A newer schema is preserved untouched and simply not executed.
        result.overrides = profile.overrides;
        return result;
    }

    struct Accepted { juce::String id; CommandScope scope; std::vector<juce::KeyPress> keys; };
    std::vector<Accepted> accepted;

    for (const auto& command : registry.list())
    {
        const bool userBound = profile.hasOverride(command.id);
        auto keys = effectiveShortcuts(command, profile);
        std::vector<juce::KeyPress> kept;
        for (const auto& key : keys)
        {
            if (! key.isValid()) continue;
            bool clash = false;
            juce::String with;
            for (const auto& other : accepted)
            {
                if (! scopesOverlap(command.scope, other.scope)) continue;
                for (const auto& existing : other.keys)
                    if (sameKey(existing, key)) { clash = true; with = other.id; break; }
                if (clash) break;
            }
            if (! clash) { kept.push_back(key); continue; }
            if (diagnostics != nullptr)
                diagnostics->add((userBound ? "Shortcut for " : "Default shortcut for ") + command.id
                                 + " (" + describeKey(key) + ") conflicts with " + with
                                 + (userBound ? "; the user binding was not applied." : "."));
        }
        if (userBound) result.overrides[command.id] = storedKeysFromKeyPresses(kept);
        accepted.push_back({ command.id, command.scope, std::move(kept) });
    }

    // Keep overrides for commands this build does not know: a component may come back later.
    for (const auto& entry : profile.overrides)
        if (result.overrides.find(entry.first) == result.overrides.end()
            && registry.find(entry.first) == nullptr)
            result.overrides[entry.first] = entry.second;
    return result;
}
} // namespace sonicator
