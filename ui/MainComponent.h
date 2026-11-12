#pragma once
#include "ui/shell/Theme.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/SearchField.h"
#include "ui/library/FolderTree.h"
#include "ui/library/TagStrip.h"
#include "ui/disc/DiscBrowserPanel.h"
#include "ui/preview/PreviewPanel.h"
#include "core/indexing/IndexingService.h"
#include "core/smart/SmartAnalysisService.h"
#include "core/tagging/TaggingService.h"
#include "core/tagging/AutoTagService.h"
#include "core/split/SplitService.h"
#include "core/extract/ExtractCandidates.h"
#include "core/extract/ExtractService.h"
#include "core/settings/AppSettings.h"
#include "core/contribute/ContributionUploader.h"
namespace sonicator {
class SplitDialog;
class ExtractDialog;
class SettingsPanel;
class FavoritesSplitter final : public juce::Component
{
public:
    FavoritesSplitter();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    std::function<int()> getHeight;
    std::function<void(int)> setHeight;
    std::function<void()> onRelease;
private:
    int dragStartHeight = 0;
};
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::DarkModeSettingListener
{
public:
    static juce::File defaultDatabaseFile();
    // hostEngine and embeddedUi are used by the DAW plugin: the processor owns the audio engine
    // (no device is opened) and dialogs render inside the editor instead of separate windows.
    explicit MainComponent(const juce::File& databaseFile = defaultDatabaseFile(),
                           bool openDevice = true,
                           AppSettings initialSettings = {},
                           juce::File settingsFile = {},
                           AudioEngine* hostEngine = nullptr,
                           bool embeddedUi = false);
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    void beginImport(const juce::File&);
    // Drops a folder (a registered root or any visible subfolder) from this catalog. Files on
    // disk stay untouched. Returns false when a background job owns the catalog.
    bool removeFolderFromLibrary(const juce::String& path);
    bool refreshIfCatalogChanged();
    juce::File backupCatalog();
    // Registered roots that do not exist on this computer.
    juce::StringArray missingRoots() const;
    void showRelocateDialog();
    // Copies (moveCurrent) or opens an existing catalog and switches every repository to it.
    bool switchCatalog(const juce::File& target, bool moveCurrent, juce::String& error);
    // Applies a whole-catalog search immediately (typing uses a 250 ms debounce instead).
    void searchLibrary(const juce::String&);
    void recordSearchHistory(const juce::String&);
    void showSearchHistory();
    void persistHideEmpty();
    void persistShowIgnored();
    void ensureSelectedFolderVisible();
    bool isFolderVisible(const juce::String& path) const;
    void persistPreviewSettings();
    // Writes the pending last folder/file immediately. Used by shutdown and session tests.
    void flushSessionState();
    void setTagFilters(std::vector<int64_t> include, std::vector<int64_t> exclude, bool matchAny = false);
private:
    SonicatorLookAndFeel look;
    juce::Image logoWordmark;
    juce::File dbPath;
    AppSettings appSettings;
    SettingsStore settingsStore;
    bool brandedLogo = false;
    AudioEngine ownedEngine;
    AudioEngine& engine;
    bool embeddedUi = false;
    PreviewPanel preview;
    FileListPanel library;
    FolderTree folders;
    FolderTree favorites;
    juce::Label favoritesLabel;
    FavoritesSplitter favoritesSplitter;
    int favoritePanelHeight = 200;
    juce::String selectedFolder;
    LibraryFilter filter;
    std::vector<Tag> allTags;
    int totalCatalogCount = 0;
    int64_t selectedSampleId = 0;
    SearchField search;
    juce::TextButton libraryMenu { "LIBRARY" };
    juce::TextButton addFolder { "+ Add folder" }, rescan { "Rescan" }, reanalyze { "Reanalyze all" }, newFiles { "New files" }, smartButton { "Smart metadata" };
    juce::TextButton pause { "Pause" }, cancel { "Cancel" }, settings { "Settings" };
    juce::TextButton tagFilter { "Tags" }, starredButton { "Starred" },
                     clearFilters { "Clear filters" }, editTags { "Edit tags..." };
    juce::ToggleButton hideEmpty { "Hide empty and unsupported" };
    juce::TextButton previousPage { "<" }, nextPage { ">" };
    juce::ComboBox pageSizeBox;
    juce::Label heading, subtitle, libraryHeading, rootsLabel, status, countLabel, analysisHeading, analysisDetails, tagsLabel, selectionStatus;
    TagStrip selectedTags;
    juce::Viewport analysisViewport;
    juce::Label stageLabel;
    std::unique_ptr<Database> readDb, writeDb;
    std::unique_ptr<SampleRepository> readRepo, writeRepo;
    std::unique_ptr<IndexingService> indexer;
    std::unique_ptr<SmartAnalysisService> smart;
    std::unique_ptr<TaggingService> tagging;
    std::unique_ptr<AutoTagService> autoTag;
    std::unique_ptr<SplitService> splitter;
    std::unique_ptr<ExtractService> extractor;
    // Guards the contribution fields, which the scheduler reads off the message thread.
    juce::CriticalSection contributionLock;
    std::shared_ptr<contribute::Uploader> contributionUploader;
    std::unique_ptr<contribute::Scheduler> contributionScheduler;
    juce::Component::SafePointer<juce::DialogWindow> splitDialog;
    juce::Component::SafePointer<SplitDialog> splitContent;
    juce::Component::SafePointer<juce::DialogWindow> extractDialog;
    juce::Component::SafePointer<ExtractDialog> extractContent;
    juce::StringArray pendingSplitScan;
    SplitOptions activeSplitOptions;
    juce::uint32 splitStartMs = 0;
    bool splitWasRunning = false;
    bool splitScanApproved = false;
    juce::uint32 extractStartMs = 0;
    bool extractWasRunning = false;
    // A mixed selection runs the embedded banks first; the disc jobs start when that batch ends.
    std::vector<DiscExtractJob> pendingDiscExtract;
    ExtractOptions pendingDiscOptions;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> audioDialog, settingsDialog, relocateDialog, autoTagDialog;
    juce::Component::SafePointer<juce::Component> embeddedSettingsHost;
    juce::Component::SafePointer<SettingsPanel> embeddedSettingsPanel;
    std::unique_ptr<DiscBrowserPanel> discBrowser;
    int pageOffset = 0, catalogCount = 0, pageSize = 10000;
    bool wasRunning = false, smartWasRunning = false, taggingWasRunning = false;
    bool autoTagPreparing = false, autoTagWasRunning = false;
    uint32_t lastRefreshMs = 0, lastSearchChange = 0, lastSessionSaveMs = 0;
    juce::String sessionSamplePath;
    bool sessionDirty = false, restoringSession = false, persistSessionState = false;
    juce::String pendingSearch;
    juce::Rectangle<int> sidebarBounds, analysisBounds, headerBounds;
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void showEmbeddedSettings();
    void closeEmbeddedSettings();
    void chooseFolder();
    void requestScan(bool reanalyze);
    void scanRoots(bool force, bool newOnly = false);
    void reloadLibrary();
    void updateTagControls();
    void selectSample(const SampleRecord&);
    void openDiscImage(const SampleRecord&);
    void ensureDiscBrowser();
    bool openSelectedDiscIfAny();
    struct EnterKeyListener final : public juce::KeyListener
    {
        explicit EnterKeyListener(MainComponent& component) : owner(component) {}
        bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    private:
        MainComponent& owner;
    };
    EnterKeyListener enterKeyListener { *this };
    void updateSelectionStatus(const SampleRecord&);
    void updateSelectedTags();
    // Removes one tag from every given sample; alt+click uses the current selection.
    void removeTagFromSamples(const std::vector<int64_t>& sampleIds, int64_t tagId);
    void showAudioSettings();
    void showSettings();
    void applySettings(const AppSettings&);
    // Consent, sending and the two panel actions. Only the application sends; the plugin shares
    // the catalog and its records leave on the next application launch.
    void startContribution();
    void askContributionConsent();
    bool contributionConsentCurrent() const;
    void setContributionEnabledFromPanel(bool wanted, juce::Component::SafePointer<SettingsPanel> panel);
    void deleteContributionData(std::function<void(bool, juce::String)> done);
    void wireContributionPanel(SettingsPanel&);
    void refreshTheme();
    void showTagFilterMenu();
    void showTagColourMenu(int64_t tagId);
    void showEditTagsMenu();
    void showSampleContextMenu(const std::vector<SampleRecord>& selection, const SampleRecord& lead, juce::Point<int> screenPosition);
    void showFolderContextMenu(const juce::String& path, bool allSamples, juce::Point<int> screenPosition);
    void showFavoriteContextMenu(const juce::String& path, juce::Point<int> screenPosition);
    void promptRenameFolder(const juce::String& path);
    void clearSampleSelection();
    void refreshFavorites();
    void toggleFavoriteFolder(const juce::String& path);
    bool isFavoriteFolder(const juce::String& path) const;
    bool isUnderFavoriteFolder(const juce::String& path) const;
    void resetTagsSelected(const std::vector<int64_t>& sampleIds);
    void resetTagsFolder(const juce::String& path, int catalogued);
    void markSessionStateDirty();
    void saveSessionState();
    // Explicit human assignment path: writes only missing links and updates the recent-tag
    // history only when at least one link was actually created.
    int assignTagAndRemember(const std::vector<int64_t>& sampleIds, int64_t tagId);
    void startAutoTagSelected(const std::vector<int64_t>& sampleIds);
    void startAutoTagFolder(const juce::String& folder);
    void showAutoTagDialog(AutoTagResult result);
    void startSplit(const std::vector<juce::File>& files);
    void finishSplit();
    void requestSplitScan(const juce::StringArray& folders);
    void closeSplitDialog();
    void requestCloseSplit();
    bool isSplitRunning() const { return splitter != nullptr && splitter->progress().running; }
    void startExtract(const std::vector<juce::File>& files,
                      const std::vector<DiscExtractJob>& discJobs = {});
    void finishExtract();
    void closeExtractDialog();
    void requestCloseExtract();
    bool isExtractRunning() const { return extractor != nullptr && extractor->progress().running; }
    void promptTagName(const juce::String& title, const juce::String& initial, std::function<void(juce::String)> onAccept);
    void promptCustomType(std::function<void(juce::String)> onAccept);
    void applyTypeOverride(const std::vector<int64_t>& ids,
                           const std::vector<SampleRecord>& files,
                           const juce::String& type);
    void triggerAdaptiveLearning(const std::vector<int64_t>& ids);
    static bool contains(const std::vector<int64_t>& values, int64_t id);
    static void toggle(std::vector<int64_t>& values, int64_t id);
    std::unique_ptr<std::thread> typeFeedbackWorker;
    std::atomic<bool> cancelFeedback{ false };
    std::unique_ptr<std::thread> featureV2Worker;
    std::atomic<bool> cancelFeatureV2{ false };
};
} // namespace sonicator
