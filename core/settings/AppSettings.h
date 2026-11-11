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
    // Display names for registered root folders. Only the tree label changes; the folder on disk
    // and every indexed path keep their real name.
    juce::StringPairArray folderAliases;
    int favoritesPanelHeight = 200;    // user-dragged divider position
    juce::String lastFolder;           // restored on the next launch when still in the catalog
    juce::String lastSamplePath;       // restored selection without starting preview playback
    bool hideEmpty = true;
    bool showIgnored = false;          // reveal catalog rows hidden by ignored_paths
    bool reasonPacksAdded = false;     // global one-time registration, not reset on root removal
    bool abletonPacksAdded = false;    // same rule for the installed Ableton factory packs
    double splitThresholdDb = -60.0;
    int splitMinSilenceMs = 250;
    int splitMinPartMs = 100;
    bool splitSameFolder = true;
    bool splitMoveToTrash = false;
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
    bool contributeTrainingData = false;
    // Consent to the contribution text above. Sending needs both the toggle and this version:
    // an earlier build promised it sent nothing, so a stored `true` alone is not consent.
    int contributionConsentVersion = 0;
    juce::String contributionInstallId;     // random UUID, created when contribution is enabled
    juce::String contributionEndpoint;      // empty means the built-in intake address
    int windowX = 0, windowY = 0, windowWidth = 0, windowHeight = 0;
    bool hasWindowBounds() const { return windowWidth > 0 && windowHeight > 0; }
    // Bump when the consent text or the payload changes in a way the user should see again.
    // Version 2: only confirmations made from this version on can be exported; rows from older
    // versions have unknown provenance and stay on the machine.
    static constexpr int currentContributionConsentVersion = 2;
    static juce::String defaultContributionEndpoint();
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
