#pragma once
#include "ShortcutPreferences.h"
#include <juce_core/juce_core.h>
#include <functional>
namespace sonicator {
enum class ThemeMode { System, Dark, Light };
// Panel sizes the user dragged. Zero width means "the adaptive default for this window size";
// an effective size is never written back here, so shrinking a window keeps the preference.
struct PanelLayoutPreferences
{
    int foldersWidth = 0;
    int analysisWidth = 0;
    int previewHeight = 156;
    int favoritesHeight = 200;
    bool operator==(const PanelLayoutPreferences& other) const
    {
        return foldersWidth == other.foldersWidth && analysisWidth == other.analysisWidth
            && previewHeight == other.previewHeight && favoritesHeight == other.favoritesHeight;
    }
    bool operator!=(const PanelLayoutPreferences& other) const { return ! (*this == other); }
};
struct AppSettings
{
    ThemeMode theme = ThemeMode::System;
    int scalePercent = 100;
    juce::String language = "English";
    int pageSize = 10000; // 0 means every match on one virtual page
    juce::String databasePath; // empty means the default catalog location
    juce::StringArray searchHistory; // most recent first, at most 10 entries
    juce::StringArray favoriteFolders; // global across catalogs, in display order
    // Display names for registered root folders. Only the tree label changes; the folder on disk
    // and every indexed path keep their real name.
    juce::StringPairArray folderAliases;
    int favoritesPanelHeight = 200;    // user-dragged divider position
    PanelLayoutPreferences standaloneLayout;
    PanelLayoutPreferences pluginLayout;
    // Last user-sized plugin editor, in logical pixels; independent of standalone bounds.
    // Zero means no remembered size yet. Project state takes precedence for restored instances.
    int pluginWindowWidth = 0, pluginWindowHeight = 0;
    ShortcutPreferences shortcuts;
    juce::String lastSettingsSection = "general";
    juce::String lastFolder;           // restored on the next launch when still in the catalog
    juce::String lastSamplePath;       // restored selection without starting preview playback
    bool hideEmpty = true;
    bool cloudView = false;       // the library opens as the tag map instead of the list
    bool showIgnored = false;          // reveal catalog rows hidden by ignored_paths
    bool reasonPacksAdded = false;     // global one-time registration, not reset on root removal
    bool abletonPacksAdded = false;    // same rule for the installed Ableton factory packs
    double splitThresholdDb = -60.0;
    int splitMinSilenceMs = 250;
    int splitMinPartMs = 100;
    bool splitSameFolder = true;
    bool splitMoveToTrash = false;
    bool splitNormalize = false;
    // Extract samples: one output folder shared by the batch, or each source's own folder.
    bool extractSameFolder = true;
    bool extractNormalize = false;
    bool extractTuneToSong = true;
    juce::String extractOutputFolder;
    // Preview panel toggles. Persisted so a restart keeps the same audition behaviour.
    bool midiPreview = false;
    bool skipSilence = false;
    bool autoPreview = true;
    bool loopPreview = false;
    bool rexGate = true;
    // Skips the Save / Normalize confirmation dialogs and replaces the original file directly.
    // Formats without a writer still get a copy.
    bool alwaysOverwriteOriginalFiles = false;
    bool contributeTrainingData = false;
    // Consent to the contribution text above. Sending needs both the toggle and this version:
    // an earlier build promised it sent nothing, so a stored `true` alone is not consent.
    int contributionConsentVersion = 0;
    juce::String contributionInstallId;     // random UUID, created when contribution is enabled
    juce::String contributionEndpoint;      // empty means the built-in intake address
    int windowX = 0, windowY = 0, windowWidth = 0, windowHeight = 0;
    bool hasWindowBounds() const { return windowWidth > 0 && windowHeight > 0; }
    // Bump when the consent text or the payload changes in a way the user should see again.
    // Version 3: an AutoTag proposal the person accepts counts as a confirmation, and tag history
    // starts at schema 15; older tag assignments are not uploaded.
    // Version 4: a confirmed row may carry the richer 84-value feature vector (schema 2).
    static constexpr int currentContributionConsentVersion = 4;
    static juce::String defaultContributionEndpoint();
    static juce::File defaultSettingsFile();
    // Tests point this at a temporary folder so a UI component that builds its own settings
    // store can never read or overwrite the owner's live preferences. Empty in the application.
    static void setTestSettingsDirectory(const juce::File&);
    static juce::File testSettingsDirectory();
    // The live settings file normally; a fresh file inside the test directory while it is set.
    static juce::File nextUiSettingsFile();
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
