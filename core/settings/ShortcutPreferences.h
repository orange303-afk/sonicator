#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace sonicator
{
// One key combination as it is stored in the settings file. The raw key code plus the four
// modifiers keep Command and Control distinct on macOS, which a text description would merge.
struct ShortcutKey
{
    int keyCode = 0;
    bool command = false;
    bool control = false;
    bool alt = false;
    bool shift = false;

    bool isValid() const { return keyCode != 0; }
    bool operator==(const ShortcutKey& other) const
    {
        return keyCode == other.keyCode && command == other.command && control == other.control
            && alt == other.alt && shift == other.shift;
    }
    bool operator!=(const ShortcutKey& other) const { return ! (*this == other); }
};

// A platform's bindings, stored as overrides. A missing id means "use the command's defaults";
// an id mapped to an empty list means "the user removed every key".
struct ShortcutProfile
{
    static constexpr int currentSchemaVersion = 1;
    int schemaVersion = currentSchemaVersion;
    // An unrecognised newer schema is kept verbatim and not executed, so an older build never
    // destroys what a newer build wrote.
    bool readable = true;
    std::map<juce::String, std::vector<ShortcutKey>> overrides;

    bool hasOverride(const juce::String& id) const { return overrides.find(id) != overrides.end(); }
};

struct ShortcutPreferences
{
    ShortcutProfile mac;
    ShortcutProfile windows;
};

// Bounded decoding: at most `maxCommands` entries and `maxKeys` keys per command, ids at most
// 256 characters. Invalid entries are skipped and reported through `diagnostics` when given.
ShortcutPreferences shortcutPreferencesFromVar(const juce::var&, juce::StringArray* diagnostics = nullptr);
juce::var shortcutPreferencesToVar(const ShortcutPreferences&);

// One line per stored command, for logs and the acceptance report.
juce::String describeShortcutProfile(const ShortcutProfile&);
} // namespace sonicator
