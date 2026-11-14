#include "ShortcutPreferences.h"

namespace sonicator
{
namespace
{
constexpr int maxCommands = 4096;
constexpr int maxKeys = 16;
constexpr int maxIdLength = 256;

ShortcutKey keyFromVar(const juce::var& value)
{
    ShortcutKey key;
    const auto* object = value.getDynamicObject();
    if (object == nullptr) return {};
    const auto code = object->getProperty("key");
    if (! code.isInt() && ! code.isInt64() && ! code.isDouble()) return {};
    key.keyCode = (int) code;
    auto flag = [object](const char* name)
    {
        const auto property = object->getProperty(name);
        return property.isBool() && (bool) property;
    };
    key.command = flag("cmd");
    key.control = flag("ctrl");
    key.alt = flag("alt");
    key.shift = flag("shift");
    if (key.keyCode <= 0) return {};
    return key;
}

juce::var keyToVar(const ShortcutKey& key)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("key", key.keyCode);
    if (key.command) object->setProperty("cmd", true);
    if (key.control) object->setProperty("ctrl", true);
    if (key.alt) object->setProperty("alt", true);
    if (key.shift) object->setProperty("shift", true);
    return juce::var(object);
}

ShortcutProfile profileFromVar(const juce::var& value, juce::StringArray* diagnostics,
                               const juce::String& platform)
{
    ShortcutProfile profile;
    const auto* object = value.getDynamicObject();
    if (object == nullptr) return profile;
    const auto version = object->getProperty("version");
    if (version.isInt() || version.isInt64() || version.isDouble())
    {
        const auto stored = (int) version;
        if (stored > ShortcutProfile::currentSchemaVersion)
        {
            // Newer schema: keep it intact, do not run or rewrite it from this build.
            profile.readable = false;
            if (diagnostics != nullptr)
                diagnostics->add(platform + ": shortcut profile schema " + juce::String(stored)
                                 + " is newer than this build; bindings are not applied.");
            return profile;
        }
    }
    const auto* bindings = object->getProperty("bindings").getDynamicObject();
    if (bindings == nullptr) return profile;
    if (bindings->getProperties().size() > maxCommands)
    {
        if (diagnostics != nullptr)
            diagnostics->add(platform + ": too many shortcut entries; extras were ignored.");
    }
    int accepted = 0;
    for (const auto& property : bindings->getProperties())
    {
        if (accepted >= maxCommands) break;
        const auto id = property.name.toString();
        if (id.isEmpty() || id.length() > maxIdLength)
        {
            if (diagnostics != nullptr)
                diagnostics->add(platform + ": shortcut entry with an invalid command id was ignored.");
            continue;
        }
        const auto* list = property.value.getArray();
        if (list == nullptr)
        {
            if (diagnostics != nullptr)
                diagnostics->add(platform + ": " + id + " is not a list and was ignored.");
            continue;
        }
        std::vector<ShortcutKey> keys;
        for (const auto& entry : *list)
        {
            if ((int) keys.size() >= maxKeys)
            {
                if (diagnostics != nullptr)
                    diagnostics->add(platform + ": " + id + " has more than "
                                     + juce::String(maxKeys) + " keys; extras were ignored.");
                break;
            }
            const auto key = keyFromVar(entry);
            if (! key.isValid())
            {
                if (diagnostics != nullptr)
                    diagnostics->add(platform + ": " + id + " has an invalid key and it was ignored.");
                continue;
            }
            keys.push_back(key);
        }
        profile.overrides[juce::String(id)] = std::move(keys);
        ++accepted;
    }
    return profile;
}

juce::var profileToVar(const ShortcutProfile& profile)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("version", profile.schemaVersion);
    auto* bindings = new juce::DynamicObject();
    for (const auto& entry : profile.overrides)
    {
        juce::Array<juce::var> keys;
        for (const auto& key : entry.second) keys.add(keyToVar(key));
        bindings->setProperty(entry.first, juce::var(keys));
    }
    object->setProperty("bindings", juce::var(bindings));
    return juce::var(object);
}
} // namespace

ShortcutPreferences shortcutPreferencesFromVar(const juce::var& value, juce::StringArray* diagnostics)
{
    ShortcutPreferences preferences;
    const auto* object = value.getDynamicObject();
    if (object == nullptr) return preferences;
    preferences.mac = profileFromVar(object->getProperty("mac"), diagnostics, "macOS");
    preferences.windows = profileFromVar(object->getProperty("windows"), diagnostics, "Windows");
    return preferences;
}

juce::var shortcutPreferencesToVar(const ShortcutPreferences& preferences)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("mac", profileToVar(preferences.mac));
    object->setProperty("windows", profileToVar(preferences.windows));
    return juce::var(object);
}

juce::String describeShortcutProfile(const ShortcutProfile& profile)
{
    juce::StringArray lines;
    lines.add("schema " + juce::String(profile.schemaVersion)
              + (profile.readable ? "" : " (not applied: newer than this build)"));
    for (const auto& entry : profile.overrides)
    {
        juce::StringArray keys;
        for (const auto& key : entry.second)
            keys.add(juce::String(key.keyCode) + (key.command ? "+cmd" : "") + (key.control ? "+ctrl" : "")
                     + (key.alt ? "+alt" : "") + (key.shift ? "+shift" : ""));
        lines.add(entry.first + ": " + (keys.isEmpty() ? juce::String("(none)") : keys.joinIntoString(", ")));
    }
    return lines.joinIntoString("\n");
}
} // namespace sonicator
