#pragma once
#include "ui/shell/Theme.h"
#include "ui/analysis/AnalysisCard.h"
#include "ui/analysis/KeyBpmBlock.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/SearchField.h"
#include "ui/library/FolderTree.h"
#include "ui/library/FolderIndexRunner.h"
#include "ui/library/TagStrip.h"
#include "ui/cloud/CloudView.h"
#include "core/cloud/CloudBuildService.h"
#include "ui/disc/DiscBrowserPanel.h"
#include "ui/preview/PreviewPanel.h"
#include "core/indexing/IndexingService.h"
#include "core/smart/SmartAnalysisService.h"
#include "core/tagging/TaggingService.h"
#include "core/tagging/AutoTagService.h"
#include "core/split/SplitService.h"
#include "core/edit/NormalizeService.h"
#include "core/extract/ExtractCandidates.h"
#include "core/extract/ExtractService.h"
#include "core/batch/BatchService.h"
#include "core/batch/BatchSources.h"
#include "core/batch/BatchPreviewService.h"
#include "core/settings/AppSettings.h"
#include "core/match/MatchPreferences.h"
#include "core/match/MatchTypes.h"
#include "plugin/HostTempoSnapshot.h"
#include "core/contribute/ContributionUploader.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/commands/ShortcutBindings.h"
#include "ui/commands/CommandRouter.h"
#include "ui/shell/PanelLayout.h"
#include "ui/shell/PanelSplitter.h"
#include "ui/shell/ThemedDialogs.h"
#include "ui/settings/SettingsSection.h"
#include <mutex>
#include <future>
namespace sonicator {
class SplitDialog;
namespace batch { class BatchProcessingDialog; }
class ExtractDialog;
class SettingsPanel;
// Blinks red while a background job runs and stays green when the app is idle. It sits left of the
// status line, so a long analysis is visible even when its counters are not.
class ActivityDot final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    ActivityDot();
    void setActive(bool);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    bool active = false, lit = true;
};
// Project state a DAW host hands to an embedded window. The application keeps the same fields in
// settings.json; a plugin project owns its own copy and receives every selection change back.
struct EmbeddedSession
{
    juce::String lastFolder;
    juce::String lastSamplePath;
    std::function<void(const juce::String& folder, const juce::String& sample)> onChanged;
};
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
                            public dialogs::EmbeddedDialogHost,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::DarkModeSettingListener
{
public:
    static juce::File defaultDatabaseFile();
    // List or cloud. The standalone app persists the choice; the plug-in keeps it per session.
    void showCloudView(bool show);
    bool cloudViewActive() const { return cloudVisible; }
    // The settings file this window reads and writes. Exposed so tests can prove they never
    // touch the owner's live preferences.
    juce::File settingsFile() const;
    // Read-only view of the command catalogue. Menus, keys and tests use the same registrations.
    const CommandRegistry& commandRegistry() const { return commands; }
    // The effective split of the panel row and the preview band in this window.
    const PanelLayoutBounds& currentLayout() const { return layoutBounds; }
    // hostEngine and embeddedUi are used by the DAW plugin: the processor owns the audio engine
    // (no device is opened) and dialogs render inside the editor instead of separate windows.
    // embeddedSession supplies the project's last folder/sample and receives changes back; the
    // embedded window never reads or writes the application's session fields.
    explicit MainComponent(const juce::File& databaseFile = defaultDatabaseFile(),
                           bool openDevice = true,
                           AppSettings initialSettings = {},
                           juce::File settingsFile = {},
                           AudioEngine* hostEngine = nullptr,
                           bool embeddedUi = false,
                           EmbeddedSession embeddedSession = {});
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    // Asks about unsaved edits before quitting; `done(true)` means it is safe to leave.
    void prepareForQuit(std::function<void(bool)> done);
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
    // Current selection, so an embedded editor can store it in the DAW project on close.
    juce::String selectedFolderPath() const { return selectedFolder; }
    juce::String selectedSamplePath() const { return sessionSamplePath; }
    // Applies a project's stored folder and sample to this open window, the way construction
    // applies them to a new one. Used when a host loads another project into a running instance.
    void applySessionSelection(const juce::String& folder, const juce::String& sample);
    // False when the catalog could not be opened (missing, not a catalog, or another build's
    // schema). The plugin then shows `catalogError()` and leaves the project state untouched.
    bool catalogIsOpen() const { return readRepo != nullptr; }
    juce::String catalogError() const { return catalogOpenError; }
    float interfaceScale() const { return appSettings.scalePercent / 100.0f; }
    // Plugin-only Match BPM / Match Scale. The processor owns the intent and the tempo snapshot;
    // this window owns the catalog lookup that turns a selected file into effective metadata.
    void setPluginMatchContext(std::function<PluginMatchPreferences()> getPreferences,
                               std::function<void (const PluginMatchPreferences&)> setPreferences,
                               std::function<plugin::HostTempoValue()> hostTempo);
    void applyPluginMatchPreferences(const PluginMatchPreferences&);
    bool matchControlsVisible() const;
    // Shared Match drag entry point for a library row and a cloud point. Returns true when the
    // gesture was handled by Match, false when the ordinary drag path should run.
    bool beginMatchedDrag(const std::vector<SampleRecord>&, juce::Component* source);
    // The plugin scales its component tree, never JUCE's process-wide Desktop (the DAW).
    std::function<void(float)> onInterfaceScaleChanged;
    // Rebuilds the library and favorites trees off the message thread and installs the result.
    void requestFolderTreeRefresh();
    void applyFolderIndex(FolderIndex);
    void applyFavoritesIndex();
    void setTagFilters(std::vector<int64_t> include, std::vector<int64_t> exclude, bool matchAny = false);
    // Restores the three panel sizes and the Favorites height to their defaults.
    void resetLayout();
    // Restores one panel to its default and keeps the other sizes.
    void resetPanelSize(const juce::String& panelId);
    // EmbeddedDialogHost: an embedded window draws dialogs inside itself, so no host-owned
    // top-level window can drift behind the DAW or close on the first click.
    bool showEmbeddedDialog(std::unique_ptr<juce::Component> content) override;
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
    EmbeddedSession embeddedSession;
    // Why the catalog is not open, when it is not. Shown by the plugin instead of an empty browser.
    juce::String catalogOpenError;
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
    studio::Chip libraryMenu { "LIBRARY" };
    juce::TextButton addFolder { "+ Add folder" }, rescan { "Rescan" }, reanalyze { "Reanalyze all" }, newFiles { "New files" }, smartButton { "Smart metadata" };
    juce::TextButton pause { "Pause" }, cancel { "Cancel" }, settings { "Settings" };
    studio::Chip tagFilter { "Tags" }, starredButton { "Starred" },
                 clearFilters { "Clear filters" };
    juce::TextButton editTags { "Edit tags..." };
    studio::CheckChip hideEmpty { "Hide empty" };
    studio::IconButton previousPage { ActionIcon::chevronLeft, "<" };
    studio::IconButton nextPage { ActionIcon::chevronRight, ">" };
    juce::ComboBox pageSizeBox;
    juce::Label heading, subtitle, libraryHeading, rootsLabel, status, countLabel, analysisHeading, tagsLabel, selectionStatus;
    AnalysisCard analysisCard;
    KeyBpmBlock keyBpm;
    ActivityDot activityDot;
    TagStrip selectedTags;
    // Cloud (tag map) view: one component, one batch query, one background build.
    studio::Chip listTab { "List", true }, cloudTab { "Cloud", true };
    std::unique_ptr<cloud::CloudView> cloudPanel;
    std::unique_ptr<cloud::CloudBuildService> cloudBuilder;
    std::unique_ptr<juce::ThreadPool> hoverPool;
    bool cloudVisible = false;
    bool cloudBuildScheduled = false;
    juce::uint32 cloudBuildAtMs = 0;
    std::vector<Tag> cloudTagSnapshot;
    std::vector<bool> cloudMatches;
    std::shared_ptr<std::atomic<uint64_t>> cloudHoverGeneration = std::make_shared<std::atomic<uint64_t>>(0);
    int64_t cloudHoverSample = 0;
    juce::Viewport analysisViewport;
    juce::Label stageLabel;
    std::unique_ptr<Database> readDb, writeDb;
    std::unique_ptr<SampleRepository> readRepo, writeRepo;
    std::optional<MatchSource> buildMatchSource(const juce::File&, int64_t& sourceSize,
                                                int64_t& sourceModified);
    // Declared after the repositories so it stops before they are destroyed.
    std::unique_ptr<FolderIndexRunner> folderIndexRunner;
    FolderIndex lastFolderIndex;
    juce::String pendingTreeSelection;
    std::unique_ptr<IndexingService> indexer;
    std::unique_ptr<SmartAnalysisService> smart;
    std::unique_ptr<TaggingService> tagging;
    std::unique_ptr<AutoTagService> autoTag;
    std::unique_ptr<SplitService> splitter;
    // Batch Processing owner: the dialog configures, the service writes, the preview auditions
    // through the existing preview panel. The owner holds the service until its thread stops.
    std::unique_ptr<batch::BatchService> batchService;
    std::unique_ptr<batch::BatchPreviewService> batchPreview;
    juce::Component::SafePointer<juce::DialogWindow> batchDialog;
    juce::Component::SafePointer<batch::BatchProcessingDialog> batchContent;
    bool batchWasRunning = false;
    bool batchClosePending = false;
    bool batchPreviewWasRunning = false;
    juce::File batchPreviewRestore;
    juce::File batchPreviewFolder;
    batch::Request pendingBatchRequest;
    bool batchPreviewAuditioned = false;
    bool pendingBatchLaunch = false;
    bool pendingPreviewStop = false;
    std::unique_ptr<ExtractService> extractor;
    std::unique_ptr<NormalizeService> normalizer;
    // Poll on the message thread; join before destroying/replacing the catalog repositories.
    std::future<FolderGenreAssignment> folderGenreWork;
    juce::String folderGenrePath, folderGenreName;
    bool folderGenreReplacement = false;
    // Guards the contribution fields, which the scheduler reads off the message thread.
    juce::CriticalSection contributionLock;
    std::shared_ptr<contribute::Uploader> contributionUploader;
    std::unique_ptr<contribute::Scheduler> contributionScheduler;
    juce::Component::SafePointer<juce::DialogWindow> splitDialog;
    juce::Component::SafePointer<SplitDialog> splitContent;
    juce::Component::SafePointer<juce::DialogWindow> extractDialog;
    juce::Component::SafePointer<ExtractDialog> extractContent;
    juce::StringArray pendingSplitScan;
    // Extract follow-up: folders to index once the batch ends, then folders to smart-analyse.
    juce::StringArray pendingExtractScan;
    SplitOptions activeSplitOptions;
    juce::uint32 splitStartMs = 0;
    bool splitWasRunning = false;
    bool splitScanApproved = false;
    juce::uint32 extractStartMs = 0;
    bool extractWasRunning = false;
    bool normalizeWasRunning = false;
    // Source path -> catalog row, so a replaced original refreshes its row and a copy inherits the
    // tags of the sample it came from.
    std::map<juce::String, int64_t> normalizeSampleIds;
    std::vector<NormalizeFileResult> normalizeResults;
    // A mixed selection runs the embedded banks first; the disc jobs start when that batch ends.
    std::vector<DiscExtractJob> pendingDiscExtract;
    ExtractOptions pendingDiscOptions;
    juce::String pendingExtractSummary;
    juce::StringArray pendingExtractProblems;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> audioDialog, settingsDialog, relocateDialog, autoTagDialog;
    juce::Component::SafePointer<juce::Component> embeddedSettingsHost;
    // A dialog opened from an embedded window; it replaces the settings overlay's role for
    // prompts and confirmations, and only one is shown at a time.
    juce::Component::SafePointer<juce::Component> embeddedDialogHost;
    void closeEmbeddedDialog();
    juce::Component::SafePointer<SettingsPanel> embeddedSettingsPanel;
    juce::Component::SafePointer<SettingsPanel> settingsPanel;
    std::unique_ptr<DiscBrowserPanel> discBrowser;
    // Every bindable action lives here. Menus, buttons and keys all resolve through it, and the
    // Hotkeys page edits the same catalogue.
    CommandRegistry commands;
    ShortcutProfile shortcutProfile;      // sanitised, what the router runs
    ShortcutProfile storedShortcutProfile; // exactly what the settings file holds
    std::unique_ptr<CommandRouter> commandRouter;
    juce::StringArray shortcutDiagnostics;
    int pageOffset = 0, catalogCount = 0, pageSize = 10000;
    bool wasRunning = false, smartWasRunning = false, taggingWasRunning = false;
    bool autoTagPreparing = false, autoTagWasRunning = false;
    uint32_t lastRefreshMs = 0, lastSearchChange = 0, lastSessionSaveMs = 0;
    juce::String sessionSamplePath;
    bool sessionDirty = false, restoringSession = false, persistSessionState = false;
    // Set at the end of construction: the first load is not a user edit, and a callback
    // then would store an empty selection in the project before the restore has run.
    bool sessionLive = false;
    juce::String pendingSearch;
    juce::Rectangle<int> sidebarBounds, analysisBounds, headerBounds, filterSeparatorBounds;
    // Effective split of the panel row and the preview band, rebuilt on every resize.
    PanelLayoutPreferences layoutPreferences;
    PanelLayoutBounds layoutBounds;
    PanelSplitter foldersDivider { PanelSplitter::Axis::columns, "folders" };
    PanelSplitter analysisDivider { PanelSplitter::Axis::columns, "analysis" };
    PanelSplitter previewDivider { PanelSplitter::Axis::rows, "preview" };
    int dragBaseFolders = 0, dragBaseAnalysis = 0, dragBasePreview = 0;
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void registerCommands();
    void persistLayout();
    void reapplyShortcutProfile();
    bool saveShortcutProfile(const ShortcutProfile&);
    CommandFocus commandFocus() const;
    bool dispatchCommandKey(const juce::KeyPress&);
    void showSettings(SettingsSection section);
    void showEmbeddedSettings();
    void closeEmbeddedSettings();
    void chooseFolder();
    void requestScan(bool reanalyze);
    void scanRoots(bool force, bool newOnly = false);
    void reloadLibrary();
    void updateTagControls();
    void setCloudMode(bool show);
    void requestCloudBuild();
    void pollCloudBuild();
    void updateCloudMatches();
    void cloudActivate(int64_t sampleId);
    void cloudHover(int64_t sampleId);
    void cloudHoverEnd();
    void persistCloudMode();
    void selectSample(const SampleRecord&);
    // The Analysis card content for one record; the only place its values are formatted.
    AnalysisContent analysisFor(const SampleRecord&) const;
    // Sets the card content and relayouts, so its height always matches the content.
    void refreshAnalysis(AnalysisContent content);
    void showAnalysisPlaceholder();
    void loadSelectedSample(const SampleRecord&);
    void clearSampleSelectionNow();
    std::optional<SampleRecord> pendingSample;
    void startCatalogSync(const juce::File&, int64_t sampleId, bool isCopy);
    void setBpmForRows(const std::vector<int64_t>&, double bpm);
    void clearBpmForRows(const std::vector<int64_t>&);
    void openDiscImage(const SampleRecord&);
    void openDiscImageNow(const SampleRecord&);
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
    void wireCommandSettings(SettingsPanel&);
    void refreshTheme();
    void showTagFilterMenu();
    void showTagColourMenu(int64_t tagId);
    void showEditTagsMenu();
    void showSampleContextMenu(const std::vector<SampleRecord>& selection, const SampleRecord& lead, juce::Point<int> screenPosition);
    void showFolderContextMenu(const juce::String& path, bool allSamples, juce::Point<int> screenPosition);
    void setGenreForFolder(const juce::String& path, const juce::String& genre, bool replaceManual = false);
    void finishFolderGenre();
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
    void openBatchProcessing(const batch::SourceSelection&);
    void startBatchProcessing();
    void launchBatch(batch::Request);
    void startBatchPreview(const batch::SourceSnapshot&, const batch::Recipe&, int generation);
    void auditionBatchPreview(const juce::File&, bool processed);
    void stopBatchPreview();
    void closeBatchDialog();
    bool isBatchRunning() const { return batchService != nullptr && batchService->isRunning(); }
    void finishSplit();
    // Normalize selected: the menu and ⇧⌘N both land here. Candidates are the ordinary files of
    // the selection that this build can decode and write.
    std::vector<SampleRecord> normalizeCandidates(const std::vector<SampleRecord>& selection);
    void startNormalizeSelected(const std::vector<SampleRecord>& selection);
    void beginNormalize(const std::vector<juce::File>& files,
                        const std::map<juce::String, int64_t>& sampleIds, bool overwriteOriginals);
    void finishNormalize();
    void syncNormalizedCatalog();
    bool isNormalizeRunning() const { return normalizer != nullptr && normalizer->progress().running; }
    // No other writer/analysis job is running, so Normalize may start and Cancel is unambiguous.
    bool normalizeJobIdle() const;
    // True when `operation` may start now. While a library scan or smart analysis runs it shows a
    // dialog that says so and offers to pause (only when `pauseIsEnough`: the operation writes new
    // files and never races the paused reader) or cancel the job; `retry` runs after that choice.
    bool analysisAllows(const juce::String& operation, bool pauseIsEnough, std::function<void()> retry);
    // The library scan or smart analysis that blocks an operation; paused jobs block only when
    // pausing is not enough.
    bool analysisBlocks(bool pauseIsEnough) const;
    void runWhenAnalysisStops(std::function<void()> retry, int attemptsLeft);
    void requestSplitScan(const juce::StringArray& folders);
    void closeSplitDialog();
    void requestCloseSplit();
    bool isSplitRunning() const { return splitter != nullptr && splitter->progress().running; }
    void startExtract(const std::vector<juce::File>& files,
                      const std::vector<DiscExtractJob>& discJobs = {});
public:
    // Pure decision used by the window itself: only disc images start an extract on drop.
    static bool isDiscDropTarget(const juce::StringArray& files);
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
private:
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
    std::unique_ptr<std::thread> catalogSyncWorker;
    // Catalog work is queued and drained by one worker so a long reanalysis never blocks the
    // message thread on a join; normalize batches and editor saves feed the same queue.
    struct CatalogSyncRequest
    {
        juce::File file;
        int64_t sampleId = 0;
        bool isCopy = false;
    };
    std::mutex catalogSyncMutex;
    std::vector<CatalogSyncRequest> catalogSyncRequests;
    bool catalogSyncQueued = false, catalogSyncStop = false;
    void enqueueCatalogSync(const juce::File&, int64_t sampleId, bool isCopy);
    void runCatalogSyncQueue(SampleRepository&, juce::Component::SafePointer<MainComponent>);
    std::unique_ptr<std::thread> typeFeedbackWorker;
    std::atomic<bool> cancelFeedback{ false };
    std::unique_ptr<std::thread> featureV2Worker;
    std::atomic<bool> cancelFeatureV2{ false };
};
} // namespace sonicator
