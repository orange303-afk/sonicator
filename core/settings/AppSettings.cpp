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
    const int pageSize = (int) object->getProperty("pageSize");
    settings.pageSize = (pageSize == 100 || pageSize == 1000 || pageSize == 10000 || pageSize == 0)
        ? pageSize : 10000;
    settings.databasePath = object->getProperty("database").toString();
    const auto hide = object->getProperty("hideEmpty");
    settings.hideEmpty = hide.isBool() && (bool) hide;
    if (const auto* history = object->getProperty("searchHistory").getArray())
        for (const auto& entry : *history)
            if (settings.searchHistory.size() < 10)
            {
                const auto text = entry.toString();
                if (text.isNotEmpty()) settings.searchHistory.add(text);
            }
    settings.windowX = (int) object->getProperty("windowX");
    settings.windowY = (int) object->getProperty("windowY");
    settings.windowWidth = juce::jmax(0, (int) object->getProperty("windowWidth"));
    settings.windowHeight = juce::jmax(0, (int) object->getProperty("windowHeight"));
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
    object->setProperty("pageSize", settings.pageSize);
    object->setProperty("database", settings.databasePath);
    object->setProperty("hideEmpty", settings.hideEmpty);
    juce::Array<juce::var> history;
    for (int i = 0; i < juce::jmin(10, settings.searchHistory.size()); ++i)
        history.add(settings.searchHistory[i]);
    object->setProperty("searchHistory", history);
    object->setProperty("windowX", settings.windowX);
    object->setProperty("windowY", settings.windowY);
    object->setProperty("windowWidth", settings.windowWidth);
    object->setProperty("windowHeight", settings.windowHeight);
    const auto text = juce::JSON::toString(juce::var(object.get()), false);
    juce::TemporaryFile temporary(path);
    if (! temporary.getFile().replaceWithText(text) || ! temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Cannot save settings");
}
void SettingsStore::update(const std::function<void(AppSettings&)>& change) const
{
    auto settings = load();
    change(settings);
    save(settings);
}
} // namespace sonicator
