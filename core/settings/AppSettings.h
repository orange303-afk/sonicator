#pragma once
#include <juce_core/juce_core.h>
#include <functional>
namespace sonicator {
enum class ThemeMode { System, Dark, Light };
struct AppSettings
{
    ThemeMode theme = ThemeMode::System;
    int scalePercent = 100;
    juce::String language = "English";
    juce::String databasePath; // empty means the default catalog location
    int windowX = 0, windowY = 0, windowWidth = 0, windowHeight = 0;
    bool hasWindowBounds() const { return windowWidth > 0 && windowHeight > 0; }
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
    // Loads the current file, applies only this change and saves. Prevents independent writers
    // (window geometry vs. theme/scale/catalog) from overwriting each other's fields.
    void update(const std::function<void(AppSettings&)>&) const;
    const juce::File& file() const { return path; }
private:
    juce::File path;
};
} // namespace sonicator
