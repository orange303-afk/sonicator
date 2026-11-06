#include "AppSettings.h"
#include <stdexcept>
namespace sonicator {
juce::File AppSettings::defaultSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/settings.json");
}
juce::String AppSettings::themeToString(ThemeMode theme)
{
    switch (theme)
    {
        case ThemeMode::Dark: return "dark";
        case ThemeMode::Light: return "light";
        case ThemeMode::System:
        default: return "system";
    }
}
ThemeMode AppSettings::themeFromString(const juce::String& value)
{
    if (value.equalsIgnoreCase("dark")) return ThemeMode::Dark;
    if (value.equalsIgnoreCase("light")) return ThemeMode::Light;
    return ThemeMode::System;
}
int AppSettings::clampScale(int value)
{
    return value >= 75 && value <= 200 ? value : 100;
}
SettingsStore::SettingsStore(juce::File file) : path(std::move(file)) {}
AppSettings SettingsStore::load() const
{
    AppSettings settings;
    if (! path.existsAsFile()) return settings;
    const auto parsed = juce::JSON::parse(path.loadFileAsString());
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr) return settings;
    settings.theme = AppSettings::themeFromString(object->getProperty("theme").toString());
    settings.scalePercent = AppSettings::clampScale((int) object->getProperty("scale"));
    const auto language = object->getProperty("language").toString();
    if (language.isNotEmpty()) settings.language = language;
    settings.databasePath = object->getProperty("database").toString();
    return settings;
}
void SettingsStore::save(const AppSettings& settings) const
{
    if (! path.getParentDirectory().createDirectory())
        throw std::runtime_error("Cannot create settings directory");
    juce::DynamicObject::Ptr object(new juce::DynamicObject());
    object->setProperty("theme", AppSettings::themeToString(settings.theme));
    object->setProperty("scale", AppSettings::clampScale(settings.scalePercent));
    object->setProperty("language", settings.language);
    object->setProperty("database", settings.databasePath);
    const auto text = juce::JSON::toString(juce::var(object.get()), false);
    juce::TemporaryFile temporary(path);
    if (! temporary.getFile().replaceWithText(text) || ! temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Cannot save settings");
}
} // namespace sonicator
