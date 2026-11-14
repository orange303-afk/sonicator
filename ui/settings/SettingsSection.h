#pragma once
#include <juce_core/juce_core.h>

namespace sonicator
{
enum class SettingsSection { general, audioMidi, hotkeys, plugins, about };

// Stable ids for the settings file; labels are translated separately.
inline juce::String settingsSectionId(SettingsSection section)
{
    switch (section)
    {
        case SettingsSection::audioMidi: return "audioMidi";
        case SettingsSection::hotkeys: return "hotkeys";
        case SettingsSection::plugins: return "plugins";
        case SettingsSection::about: return "about";
        case SettingsSection::general:
        default: return "general";
    }
}
inline SettingsSection settingsSectionFromId(const juce::String& id)
{
    if (id == "audioMidi") return SettingsSection::audioMidi;
    if (id == "hotkeys") return SettingsSection::hotkeys;
    if (id == "plugins") return SettingsSection::plugins;
    if (id == "about") return SettingsSection::about;
    return SettingsSection::general;
}
} // namespace sonicator
