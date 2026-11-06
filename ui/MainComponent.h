#pragma once
#include "ui/shell/Theme.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/FolderTree.h"
#include "ui/library/TagStrip.h"
#include "ui/preview/PreviewPanel.h"
#include "core/indexing/IndexingService.h"
#include "core/settings/AppSettings.h"
namespace sonicator {
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::DarkModeSettingListener
{
public:
    static juce::File defaultDatabaseFile();
    explicit MainComponent(const juce::File& databaseFile = defaultDatabaseFile(),
                           bool openDevice = true,
                           AppSettings initialSettings = {},
                           juce::File settingsFile = {});
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& k) override { return preview.keyPressed(k); }
    void beginImport(const juce::File&);
    bool refreshIfCatalogChanged();
    juce::File backupCatalog();
    // Copies (moveCurrent) or opens an existing catalog and switches every repository to it.
    bool switchCatalog(const juce::File& target, bool moveCurrent, juce::String& error);
    // Applies a whole-catalog search immediately (typing uses a 250 ms debounce instead).
    void searchLibrary(const juce::String&);
    void setTagFilters(std::vector<int64_t> include, std::vector<int64_t> exclude, bool matchAny = false);
private:
    SonicatorLookAndFeel look;
    juce::Image logoWordmark;
    juce::File dbPath;
    AppSettings appSettings;
    SettingsStore settingsStore;
    bool brandedLogo = false;
    AudioEngine engine;
    PreviewPanel preview;
    FileListPanel library;
    FolderTree folders;
    juce::String selectedFolder;
    LibraryFilter filter;
    std::vector<Tag> allTags;
    int totalCatalogCount = 0;
    int64_t selectedSampleId = 0;
    juce::TextEditor search;
    juce::TextButton addFolder { "+ Add folder" }, rescan { "Rescan" }, reanalyze { "Reanalyze all" }, newFiles { "New files" };
    juce::TextButton pause { "Pause" }, cancel { "Cancel" }, settings { "Settings" }, backup { "Backup" };
    juce::TextButton tagFilter { "Tags" }, starredButton { "Starred" }, clearFilters { "Clear filters" }, editTags { "Edit tags..." };
    juce::TextButton previousPage { "<" }, nextPage { ">" };
    juce::Label heading, subtitle, libraryHeading, rootsLabel, status, countLabel, analysisHeading, analysisDetails, tagsLabel;
    TagStrip selectedTags;
    juce::Viewport analysisViewport;
    juce::Label stageLabel;
    std::unique_ptr<Database> readDb, writeDb;
    std::unique_ptr<SampleRepository> readRepo, writeRepo;
    std::unique_ptr<IndexingService> indexer;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> audioDialog, settingsDialog;
    int pageOffset = 0, catalogCount = 0;
    bool wasRunning = false;
    uint32_t lastRefreshMs = 0, lastSearchChange = 0;
    juce::String pendingSearch;
    juce::Rectangle<int> sidebarBounds, analysisBounds, headerBounds;
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void chooseFolder();
    void requestScan(bool reanalyze);
    void scanRoots(bool force, bool newOnly = false);
    void reloadLibrary();
    void updateTagControls();
    void selectSample(const SampleRecord&);
    void updateSelectedTags();
    void showAudioSettings();
    void showSettings();
    void applySettings(const AppSettings&);
    void refreshTheme();
    void showTagFilterMenu();
    void showTagColourMenu(int64_t tagId);
    void showEditTagsMenu();
    void showSampleContextMenu(const std::vector<SampleRecord>& selection, const SampleRecord& lead, juce::Point<int> screenPosition);
    void promptTagName(const juce::String& title, const juce::String& initial, std::function<void(juce::String)> onAccept);
    static bool contains(const std::vector<int64_t>& values, int64_t id);
    static void toggle(std::vector<int64_t>& values, int64_t id);
};
} // namespace sonicator
