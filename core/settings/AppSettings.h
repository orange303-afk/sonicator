#pragma once
#include <juce_core/juce_core.h>
namespace sonicator {
enum class ThemeMode { System, Dark, Light };
struct AppSettings
{
    ThemeMode theme = ThemeMode::System;
    int scalePercent = 100;
    juce::String language = "English";
    juce::String databasePath; // empty means the default catalog location
    static juce::File defaultSettingsFile();
    static juce::String themeToString(ThemeMode);
    static ThemeMode themeFromString(const juce::String&);
    static int clampScale(int);
};
class SettingsStore
{
public:
    explicit SettingsStore(juce::File file = AppSettings::defaultSettingsFile());
    AppSettings load() const;
    void save(const AppSettings&) const;
    const juce::File& file() const { return path; }
private:
    juce::File path;
};
} // namespace sonicator
