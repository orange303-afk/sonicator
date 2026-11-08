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
    int pageSize = 10000; // 0 means every match on one virtual page
    juce::String databasePath; // empty means the default catalog location
    juce::StringArray searchHistory; // most recent first, at most 10 entries
    juce::StringArray favoriteFolders; // global across catalogs, in display order
    int favoritesPanelHeight = 200;    // user-dragged divider position
    juce::String lastFolder;           // restored on the next launch when still in the catalog
    juce::String lastSamplePath;       // restored selection without starting preview playback
    bool hideEmpty = true;
    bool showIgnored = false;          // reveal catalog rows hidden by ignored_paths
    // Preview panel toggles. Persisted so a restart keeps the same audition behaviour.
    bool midiPreview = false;
    bool skipSilence = false;
    bool autoPreview = true;
    bool loopPreview = false;
    bool rexGate = true;
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
