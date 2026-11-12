#include "MainComponent.h"
#include "core/dsp/FeatureExtractorV2.h"
#include "core/ml/FeatureV2Backfill.h"
#include "core/ableton/AbletonPacks.h"
#include "core/reason/ReasonPatch.h"
#include "ui/tagging/AutoTagDialog.h"
#include "ui/split/SplitDialog.h"
#include "ui/extract/ExtractDialog.h"
#include "core/extract/EmbeddedSamples.h"
#include "core/scanner/FileScanner.h"
#include "BinaryData.h"
#include "core/database/PathRelocation.h"
#include "ui/settings/RelocateFoldersPanel.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/shell/ThemedDialogs.h"
#include "ui/platform/FileClipboard.h"
#include "core/dsp/AnalysisAudio.h"
#include "core/smart/AcousticClassifierService.h"
#include "core/smart/NameHeuristics.h"
#include "core/Version.h"
#include "core/ml/AdaptiveClassifier.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <thread>
namespace sonicator {
namespace {
// The file ships in the bundle (an empty label list when there is not enough data), so a broken
// resource degrades to today's local-only behaviour instead of stopping startup.
void loadShippedGlobalPrototypes()
{
    auto& classifier = AcousticClassifierService::getSharedAdaptiveClassifier();
    const auto json = juce::String::fromUTF8(BinaryData::global_prototypes_json,
                                             BinaryData::global_prototypes_jsonSize);
    if (! classifier.loadGlobalPrototypes(json))
        juce::Logger::writeToLog("Global prototypes were not loaded; using local feedback only");
    const auto model = juce::String::fromUTF8(BinaryData::global_model_json,
                                              BinaryData::global_model_jsonSize);
    if (! classifier.loadGlobalModel(model))
        juce::Logger::writeToLog("Global model was not loaded; schema-2 queries use local prototypes only");
}
bool isDiscImageFormat(const juce::String& format)
{
    return format == "ISO" || format == "AKAI" || format == "CUE" || format == "MDX" || format == "BIN" || format == "IMG";
}
}
namespace {
struct PaletteColour { const char* name; uint32_t colour; };
constexpr PaletteColour tagPalette[] = {
    { "Red", 0xffe05a5a }, { "Orange", 0xffe08a45 }, { "Yellow", 0xffe0b84a },
    { "Lime", 0xff9ad06a }, { "Green", 0xff6ad0a0 }, { "Teal", 0xff4fb8c8 },
    { "Blue", 0xff4f8fd0 }, { "Violet", 0xff7a6aea }, { "Purple", 0xffa96ae0 },
    { "Pink", 0xffd06ab8 }, { "Rose", 0xffd06a7a }, { "Grey", 0xff8a8f9c }
};
constexpr int pageSize = 10000;
// Catalog rows for formats this build no longer indexes: archive metadata rows and older error
// rows for protected Ableton AIFC samples. Source files are never opened beyond their headers.
int purgeUnsupportedRows(SampleRepository& repo)
{
    int removed = repo.removeSamplesWithFormats({ "CMB", "RFL" });
    juce::StringArray protectedRows;
    for (const auto& path : repo.errorPathsWithFormats({ "AIF", "AIFF", "AIFC" }))
        if (isAbletonProtectedSample(juce::File(path))) protectedRows.add(path);
    if (! protectedRows.isEmpty())
    {
        removed += protectedRows.size();
        repo.removeByPaths(protectedRows);
    }
    return removed;
}
juce::String canonicalFolderPath(const juce::String& path)
{
    return path.isEmpty() ? juce::String() : juce::File(path).getFullPathName();
}
juce::Image tagDotImage(uint32_t colour)
{
    juce::Image image(juce::Image::ARGB, 14, 14, true);
    juce::Graphics g(image);
    g.setColour(juce::Colour(colour)); g.fillEllipse(2.0f, 2.0f, 10.0f, 10.0f);
    return image;
}
juce::Image starMenuImage()
{
    juce::Image image(juce::Image::ARGB, 14, 14, true);
    juce::Graphics g(image);
    juce::Path path;
    const auto centre = juce::Point<float>(7.0f, 7.0f);
    for (int i = 0; i < 10; ++i)
    {
        const float angle = -juce::MathConstants<float>::halfPi + i * juce::MathConstants<float>::pi / 5.0f;
        const float radius = (i % 2 == 0) ? 6.0f : 2.7f;
        const juce::Point<float> point(centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius);
        if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
    }
    path.closeSubPath();
    g.setColour(theme::text); g.fillPath(path);
    return image;
}
// Renders a dialog inside the editor instead of as a separate window. Plugin hosts do not
// reliably keep top-level modal windows from a plugin, and a modal window that dismisses on
// "input outside" closes on the first click inside a DAW.
class EmbeddedDialogOverlay final : public juce::Component
{
public:
    explicit EmbeddedDialogOverlay(std::unique_ptr<juce::Component> contentToOwn)
        : content(std::move(contentToOwn))
    {
        addAndMakeVisible(*content);
        setWantsKeyboardFocus(true);
    }
    std::function<void()> onDismiss;
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.6f));
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && onDismiss != nullptr) { onDismiss(); return true; }
        return false;
    }
    void resized() override
    {
        if (content == nullptr || getWidth() <= 0 || getHeight() <= 0) return;
        const auto available = getLocalBounds().reduced(24);
        const auto width = (float) content->getWidth();
        const auto height = (float) content->getHeight();
        if (width <= 0 || height <= 0) return;
        const auto scale = juce::jmin(1.0f, juce::jmin((float) available.getWidth() / width,
                                                       (float) available.getHeight() / height));
        content->setTransform(juce::AffineTransform::scale(scale));
        const auto target = available.withSizeKeepingCentre(juce::roundToInt(width * scale),
                                                            juce::roundToInt(height * scale));
        content->setBounds(target.getX(), target.getY(), juce::roundToInt(width), juce::roundToInt(height));
    }
private:
    std::unique_ptr<juce::Component> content;
};
// A modal Settings window that dismisses itself when the user clicks anywhere outside it.
class SettingsDialogWindow final : public juce::DialogWindow
{
public:
    SettingsDialogWindow() : juce::DialogWindow("Settings", theme::panel, true) {}
    void closeButtonPressed() override { exitModalState(0); }
    void inputAttemptWhenModal() override { exitModalState(0); }
};
class RelocateDialogWindow final : public juce::DialogWindow
{
public:
    RelocateDialogWindow() : juce::DialogWindow("Relocate folders", theme::panel, true) {}
    void closeButtonPressed() override { exitModalState(0); }
    void inputAttemptWhenModal() override { exitModalState(0); }
};
// The split window owns its content and lets the owner decide what the close button means while
// a job is running: it cancels the job and leaves the window open with its result.
class SplitDialogWindow final : public juce::DialogWindow
{
public:
    SplitDialogWindow() : juce::DialogWindow("Split by silence", theme::panel, true) {}
    void closeButtonPressed() override { if (onClose) onClose(); }
    void inputAttemptWhenModal() override {}
    std::function<void()> onClose;
};
class ExtractDialogWindow final : public juce::DialogWindow
{
public:
    ExtractDialogWindow() : juce::DialogWindow("Extract samples", theme::panel, true) {}
    void closeButtonPressed() override { if (onClose) onClose(); }
    void inputAttemptWhenModal() override {}
    std::function<void()> onClose;
};
}
FavoritesSplitter::FavoritesSplitter()
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}
void FavoritesSplitter::paint(juce::Graphics& g)
{
    g.setColour(theme::isDark ? theme::line : theme::selection);
    const auto area = getLocalBounds().reduced(6, 3).toFloat();
    g.fillRoundedRectangle(area, 2.0f);
    if (isMouseOver())
    {
        g.setColour(theme::accent.withAlpha(0.9f));
        g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth(), 2.0f), 1.0f);
    }
}
void FavoritesSplitter::mouseDown(const juce::MouseEvent&)
{
    dragStartHeight = getHeight != nullptr ? getHeight() : 0;
}
void FavoritesSplitter::mouseDrag(const juce::MouseEvent& e)
{
    if (setHeight != nullptr)
        setHeight(dragStartHeight - e.getDistanceFromDragStartY());
}
void FavoritesSplitter::mouseUp(const juce::MouseEvent&)
{
    if (onRelease != nullptr) onRelease();
}
void FavoritesSplitter::mouseEnter(const juce::MouseEvent&) { repaint(); }
void FavoritesSplitter::mouseExit(const juce::MouseEvent&) { repaint(); }
juce::File MainComponent::defaultDatabaseFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/sonicator.sqlite");
}
MainComponent::MainComponent(const juce::File& databaseFile,bool openDevice,AppSettings initialSettings,
                             juce::File settingsFile,AudioEngine* hostEngine,bool embeddedUiMode)
    : dbPath(databaseFile), appSettings(std::move(initialSettings)),
      settingsStore(settingsFile != juce::File() ? std::move(settingsFile) : AppSettings::defaultSettingsFile()),
      ownedEngine(openDevice && hostEngine == nullptr),
      engine(hostEngine != nullptr ? *hostEngine : ownedEngine),
      embeddedUi(embeddedUiMode),
      preview(engine,dbPath.getParentDirectory().getChildFile("cache"))
{
    theme::apply(appSettings.theme);
    persistSessionState = juce::JUCEApplicationBase::getInstance() != nullptr;
    setLookAndFeel(&look); look.refreshColours(); setWantsKeyboardFocus(true);
    logoWordmark = juce::ImageFileFormat::loadFrom(BinaryData::logosilver_png, BinaryData::logosilver_pngSize);
    for (auto* c : std::array<juce::Component*,32>{&preview,&library,&search,&addFolder,&rescan,&reanalyze,&newFiles,&smartButton,
         &pause,&cancel,&settings,&tagFilter,&starredButton,&hideEmpty,&clearFilters,&editTags,&previousPage,&nextPage,&pageSizeBox,&heading,&subtitle,
         &libraryHeading,&rootsLabel,&status,&countLabel,&analysisHeading,&analysisDetails,&tagsLabel,
         &selectedTags,&analysisViewport,&stageLabel,&selectionStatus})
        if (c != nullptr) addAndMakeVisible(c);
    heading.setText("SONICATOR",juce::dontSendNotification);
    heading.setFont(juce::FontOptions(24,juce::Font::bold));
    subtitle.setText("INTELLIGENT SAMPLE EXPLORER",juce::dontSendNotification);
    subtitle.setFont(juce::FontOptions(11)); subtitle.setColour(juce::Label::textColourId,theme::muted);
    brandedLogo = logoWordmark.isValid();
    heading.setVisible(! brandedLogo); subtitle.setVisible(! brandedLogo);
    libraryHeading.setText("LIBRARY",juce::dontSendNotification);
    libraryHeading.setColour(juce::Label::textColourId,theme::muted);
    rootsLabel.setText("Add your sample folders.\nOriginal files stay untouched.",juce::dontSendNotification);
    rootsLabel.setJustificationType(juce::Justification::topLeft);
    rootsLabel.setColour(juce::Label::textColourId,theme::muted); rootsLabel.setFont(juce::FontOptions(12));
    analysisHeading.setText("Sample Analysis",juce::dontSendNotification);
    analysisHeading.setFont(juce::FontOptions(17,juce::Font::bold));
    analysisDetails.setText("Select a sample to inspect its measured audio features.",juce::dontSendNotification);
    analysisDetails.setJustificationType(juce::Justification::topLeft); analysisDetails.setFont(juce::FontOptions(13));
    analysisViewport.setViewedComponent(&analysisDetails,false);
    analysisViewport.setScrollBarsShown(true,false);
    addChildComponent(preview.containerSourceList());
    preview.onSourcesChanged = [this] { resized(); };
    stageLabel.setText("SMART METADATA\n\nType, BPM and Key are in the\nlibrary columns. Smart metadata\nanalyzes new or changed files.",juce::dontSendNotification);
    stageLabel.setFont(juce::FontOptions(11)); stageLabel.setColour(juce::Label::textColourId,theme::muted);
    stageLabel.setJustificationType(juce::Justification::topLeft);
    status.setFont(juce::FontOptions(12)); status.setColour(juce::Label::textColourId,theme::muted);
    countLabel.setFont(juce::FontOptions(12));
    tagsLabel.setText("TAGS",juce::dontSendNotification);
    tagsLabel.setFont(juce::FontOptions(11)); tagsLabel.setColour(juce::Label::textColourId,theme::muted);
    search.setTextToShowWhenEmpty("Search: *.rx2, ext:wav, type:pad, key:Am, C4",theme::muted);
    search.setJustification(juce::Justification::centredLeft);
    search.setIndents(10, 0);
    search.onTextChange = [this]
    {
        pendingSearch = search.getText();
        lastSearchChange = juce::Time::getMillisecondCounter();
    };
    search.onReturnKey = [this]
    {
        pendingSearch = search.getText();
        filter.text = pendingSearch.trim(); pageOffset = 0; reloadLibrary();
        recordSearchHistory(filter.text);
    };
    search.onFocusLost = [this] { recordSearchHistory(search.getText().trim()); };
    search.setTooltip("Click for the last 10 searches; Escape dismisses history");
    search.onHistoryRequest = [this] { showSearchHistory(); };
    search.onEscapeKey = [this]
    {
        search.clear(); pendingSearch.clear(); filter.text.clear(); pageOffset = 0; reloadLibrary();
    };
    search.onClear = search.onEscapeKey;
    search.setComponentID("search");
    selectedTags.setComponentID("selectedTags");
    preview.setComponentID("preview");
    library.setComponentID("library"); status.setComponentID("importStatus"); analysisDetails.setComponentID("analysisDetails");
    selectionStatus.setComponentID("selectionStatus");
    selectionStatus.setFont(juce::FontOptions(11));
    selectionStatus.setColour(juce::Label::textColourId,theme::muted);
    selectionStatus.setJustificationType(juce::Justification::centredLeft);
    selectionStatus.setText("No sample selected",juce::dontSendNotification);
    library.onSelected = [this](const SampleRecord& r) { selectSample(r); };
    library.onTagAltClicked = [this](int64_t sampleId, int64_t tagId)
    {
        std::vector<int64_t> ids { sampleId };
        if (library.isRowSelected(library.getSelectedRow()))
        {
            ids.clear();
            for (const auto& sample : library.getSelectedSamples()) ids.push_back(sample.id);
        }
        removeTagFromSamples(ids, tagId);
    };
    selectedTags.onTagClicked = [this](const Tag& tag, const juce::ModifierKeys& mods)
    {
        if (! mods.isAltDown()) return;
        std::vector<int64_t> ids;
        for (const auto& sample : library.getSelectedSamples()) ids.push_back(sample.id);
        if (ids.empty() && selectedSampleId > 0) ids.push_back(selectedSampleId);
        removeTagFromSamples(ids, tag.id);
    };
    library.onOpen = [this](const SampleRecord& r) { openDiscImage(r); };
    library.onDragMessage = [this](const juce::String& message) { status.setText(message,juce::dontSendNotification); };
    library.onContextMenu = [this](const std::vector<SampleRecord>& selection, const SampleRecord& lead, juce::Point<int> position)
    {
        showSampleContextMenu(selection, lead, position);
    };
    library.onSortChanged = [this](int column, bool ascending)
    {
        switch (column)
        {
            case 2: filter.sort = LibrarySort::Format; break;
            case 3: filter.sort = LibrarySort::Duration; break;
            case 4: filter.sort = LibrarySort::Rate; break;
            case 5: filter.sort = LibrarySort::Channels; break;
            case 6: filter.sort = LibrarySort::Status; break;
            case 10: filter.sort = LibrarySort::Type; break;
            case 11: filter.sort = LibrarySort::Bpm; break;
            case 12: filter.sort = LibrarySort::Key; break;
            default: filter.sort = LibrarySort::Name; break;
        }
        filter.ascending = ascending; pageOffset = 0; reloadLibrary();
    };
    addAndMakeVisible(folders); rootsLabel.setVisible(false);
    folders.setComponentID("folderTree");
    folders.setFolderAliases(appSettings.folderAliases);
    addAndMakeVisible(favoritesLabel); addAndMakeVisible(favorites); addAndMakeVisible(favoritesSplitter);
    favorites.setFolderAliases(appSettings.folderAliases);
    favoritesLabel.setText("FAVORITES",juce::dontSendNotification);
    favoritesLabel.setFont(juce::FontOptions(11)); favoritesLabel.setColour(juce::Label::textColourId,theme::muted);
    favoritesLabel.setComponentID("favoritesLabel"); favorites.setComponentID("favoritesTree");
    favoritesSplitter.setComponentID("favoritesSplitter");
    favoritesLabel.setVisible(false); favorites.setVisible(false); favoritesSplitter.setVisible(false);
    favoritePanelHeight = appSettings.favoritesPanelHeight;
    favoritesSplitter.getHeight = [this] { return favoritePanelHeight; };
    favoritesSplitter.setHeight = [this](int height)
    {
        favoritePanelHeight = juce::jlimit(60, 2000, height);
        resized();
    };
    favoritesSplitter.onRelease = [this]
    {
        try { settingsStore.update([this](AppSettings& saved) { saved.favoritesPanelHeight = favoritePanelHeight; }); }
        catch (const std::exception& e)
        {
            status.setText("Could not save the favorites divider: " + juce::String::fromUTF8(e.what()),
                juce::dontSendNotification);
        }
    };
    folders.onFolder = [this](juce::String path)
    {
        favorites.clearSelection();
        if (juce::File::isAbsolutePath(path) && juce::File(path).hasFileExtension("iso;bin;mdx;cue;akai;img"))
        {
            SampleRecord image;
            image.filePath = path;
            image.format = juce::File(path).getFileExtension().substring(1).toUpperCase();
            openDiscImage(image);
            return;
        }
        if (discBrowser) { discBrowser->closeImage(); discBrowser->setVisible(false); }
        selectedFolder = std::move(path); pageOffset = 0;
        sessionSamplePath.clear(); markSessionStateDirty();
        reloadLibrary();
    };
    folders.onDiscFolder = [this](const juce::String& image, DiscEntryId directory)
    {
        if (! discBrowser || ! discBrowser->canReuseImage(juce::File(image)))
        {
            SampleRecord record;
            record.filePath = image;
            record.format = juce::File(image).getFileExtension().substring(1).toUpperCase();
            openDiscImage(record);
        }
        if (discBrowser)
        {
            discBrowser->setVisible(true);
            discBrowser->showDirectory(directory);
        }
    };
    folders.onContextMenu = [this](const juce::String& path, bool allSamples, juce::Point<int> position)
    {
        showFolderContextMenu(path, allSamples, position);
    };
    favorites.onFolder = [this](juce::String path)
    {
        if (path.isEmpty()) return;
        if (discBrowser) { discBrowser->closeImage(); discBrowser->setVisible(false); }
        folders.clearSelection();
        selectedFolder = std::move(path); pageOffset = 0;
        sessionSamplePath.clear(); markSessionStateDirty();
        reloadLibrary();
    };
    favorites.onContextMenu = [this](const juce::String& path, bool, juce::Point<int> position)
    {
        showFavoriteContextMenu(path, position);
    };
    addFolder.onClick = [this] { chooseFolder(); };
    rescan.onClick = [this] { requestScan(false); };
    newFiles.onClick = [this] { scanRoots(false, true); };
    newFiles.setButtonText("Rescan and analyze new files");
    reanalyze.onClick = [this] { requestScan(true); };
    smartButton.onClick = [this]
    {
        if (! smart || ! readRepo) return;
        if (isSplitRunning())
        {
            status.setText("Wait for Split to finish.",juce::dontSendNotification);
            return;
        }
        if (indexer && indexer->progress().running)
        {
            status.setText("Wait for the library scan to finish before smart analysis.",juce::dontSendNotification);
            return;
        }
        if (smart->progress().running)
        {
            status.setText("Smart metadata analysis is already running.", juce::dontSendNotification);
            return;
        }
        if (! smart->start())
            status.setText("Smart metadata analysis could not start.",juce::dontSendNotification);
    };
    smartButton.setTooltip("Read Type / BPM / Key metadata for missing or changed files in the background");
    addAndMakeVisible(libraryMenu);
    libraryMenu.setComponentID("libraryMenu");
    libraryMenu.setWantsKeyboardFocus(false);
    libraryMenu.setMouseClickGrabsKeyboardFocus(false);
    for (auto* c : { &addFolder, &rescan, &newFiles, &reanalyze, &smartButton }) c->setVisible(false);
    libraryHeading.setVisible(false);
    libraryMenu.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel(&look);
        // Keep every item clickable even while a background job runs. The actions report what to
        // wait for instead of leaving the whole menu grey and apparently broken.
        menu.addItem(1, "Add folder...");
        menu.addItem(2, "Rescan");
        menu.addItem(3, "Rescan and analyze new files");
        menu.addSeparator();
        menu.addItem(4, "Reanalyze all");
        menu.addItem(5, "Smart metadata");
        menu.addSeparator();
        menu.addItem(6, "Show ignored", true, filter.showIgnored);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&libraryMenu),
            [safe = juce::Component::SafePointer<MainComponent>(this)](int result)
            {
                if (! safe) return;
                switch (result)
                {
                    case 1: safe->chooseFolder(); break;
                    case 2: safe->requestScan(false); break;
                    case 3: safe->scanRoots(false, true); break;
                    case 4: safe->requestScan(true); break;
                    case 5: safe->smartButton.onClick(); break;
                    case 6:
                        if (safe->readRepo == nullptr) break;
                        safe->filter.showIgnored = ! safe->filter.showIgnored;
                        safe->persistShowIgnored();
                        safe->pageOffset = 0;
                        safe->ensureSelectedFolderVisible();
                        safe->reloadLibrary();
                        safe->folders.refresh(*safe->readRepo, safe->readRepo->roots(), true,
                                              safe->filter.showIgnored);
                        safe->refreshFavorites();
                        break;
                    default: break;
                }
            });
    };
    pause.onClick = [this]
    {
        // Pause and Cancel act on whichever background job is running.
        if (indexer && indexer->progress().running)
            indexer->setPaused(! indexer->progress().paused);
        else if (smart && smart->progress().running)
            smart->setPaused(! smart->progress().paused);
    };
    cancel.onClick = [this]
    {
        if (splitter && splitter->progress().running) splitter->cancel();
        else if (indexer && indexer->progress().running) indexer->cancel();
        else if (smart && smart->progress().running) smart->cancel();
        else if (tagging && tagging->progress().running) tagging->cancel();
        else if (autoTag) autoTag->cancel();
    };
    settings.onClick = [this] { showSettings(); };
    tagFilter.onClick = [this] { showTagFilterMenu(); };
    starredButton.onClick = [this]
    {
        filter.starredOnly = ! filter.starredOnly; pageOffset = 0; reloadLibrary();
    };
    hideEmpty.setComponentID("hideEmpty");
    filter.hideEmpty = appSettings.hideEmpty;
    filter.showIgnored = appSettings.showIgnored;
    hideEmpty.setToggleState(filter.hideEmpty,juce::dontSendNotification);
    hideEmpty.setTooltip("Hide files quieter than -60 dBFS (peak below 0.001)");
    hideEmpty.onClick = [this]
    {
        filter.hideEmpty = hideEmpty.getToggleState();
        persistHideEmpty(); pageOffset = 0; reloadLibrary();
    };
    clearFilters.onClick = [this]
    {
        search.clear(); pendingSearch.clear();
        filter.text.clear(); filter.includeTags.clear(); filter.excludeTags.clear();
        filter.matchAnyTag = false; filter.starredOnly = false;
        filter.hideDuplicates = false; filter.hideEmpty = false; filter.duplicateHash.clear();
        persistHideEmpty();
        pageOffset = 0; reloadLibrary();
    };
    editTags.onClick = [this] { showEditTagsMenu(); };
    pageSize = appSettings.pageSize;
    pageSizeBox.setComponentID("pageSize");
    pageSizeBox.addItem("100",100); pageSizeBox.addItem("1 000",1000);
    pageSizeBox.addItem("10 000",10000); pageSizeBox.addItem("All",1000000);
    pageSizeBox.setSelectedId(pageSize == 0 ? 1000000 : pageSize,juce::dontSendNotification);
    pageSizeBox.setTooltip("Rows shown on one page");
    pageSizeBox.onChange = [this]
    {
        const auto selected = pageSizeBox.getSelectedId();
        pageSize = selected == 1000000 ? 0 : selected;
        pageOffset = 0;
        try { settingsStore.update([this](AppSettings& saved) { saved.pageSize = pageSize; }); } catch (...) {}
        reloadLibrary();
        if (isShowing()) library.focusForNavigation();
    };
    previousPage.onClick = [this] { pageOffset = juce::jmax(0,pageOffset-pageSize); reloadLibrary(); };
    nextPage.onClick = [this] { pageOffset += pageSize; reloadLibrary(); };
    preview.onAction = [this] { if (isShowing()) library.focusForNavigation(); };
    preview.onSettingsChanged = [this] { persistPreviewSettings(); };
    preview.applyPersistedSettings(appSettings);
    for (auto* button : std::array<juce::Button*,15>{&addFolder,&rescan,&reanalyze,&newFiles,&smartButton,&pause,&cancel,&settings,
          &tagFilter,&starredButton,&hideEmpty,&clearFilters,&editTags,&previousPage,&nextPage})
    {
        button->setWantsKeyboardFocus(false); button->setMouseClickGrabsKeyboardFocus(false);
        if (button == &addFolder || button == &settings) continue; // Modal dialogs must retain their own focus.
        auto action = button->onClick;
        button->onClick = [this,action] { if (action) action(); if (isShowing()) library.focusForNavigation(); };
    }
    editTags.setEnabled(false);
    clearFilters.setEnabled(false);
    // Lay out before the first tree build: JUCE sizes a TreeView's scrollable content when items
    // are created, so a tree populated while it is still 0x0 keeps truncating its rows.
    setSize(1280,800);
    try
    {
        writeDb = std::make_unique<Database>(dbPath); writeRepo = std::make_unique<SampleRepository>(*writeDb);
        writeRepo->createTables();
        // Formats this build no longer indexes leave the catalog; the files stay on disk.
        if (purgeUnsupportedRows(*writeRepo) > 0)
            writeRepo->refreshDuplicates();
        readDb = std::make_unique<Database>(dbPath); readRepo = std::make_unique<SampleRepository>(*readDb);
        AcousticClassifierService::getSharedAdaptiveClassifier().loadFromRepository(*readRepo);
        loadShippedGlobalPrototypes();
        indexer = std::make_unique<IndexingService>(*writeRepo);
        // Only a real application startup may register the user's system pack folders.
        if (persistSessionState)
        {
            const auto reasonPacks=juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Library/Application Support/Propellerhead Software/Packs");
            const auto abletonPacks=juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile("Ableton/Factory Packs");
            juce::StringArray packRoots;
            if (registerReasonPacks(*writeRepo,settingsStore,reasonPacks))
                packRoots.add(reasonPacks.getFullPathName());
            if (registerAbletonPacks(*writeRepo,settingsStore,abletonPacks))
                packRoots.add(abletonPacks.getFullPathName());
            if (! packRoots.isEmpty() && indexer->start(packRoots)) wasRunning=true;
            const auto saved=settingsStore.load();
            appSettings.reasonPacksAdded=saved.reasonPacksAdded;
            appSettings.abletonPacksAdded=saved.abletonPacksAdded;
        }
        smart = std::make_unique<SmartAnalysisService>(*writeRepo);
        tagging = std::make_unique<TaggingService>(*writeRepo);
        tagging->onTagsChanged = [safe = juce::Component::SafePointer<MainComponent>(this)](std::vector<int64_t> ids)
        {
            juce::MessageManager::callAsync([safe, ids = std::move(ids)]
            {
                if (safe != nullptr) safe->triggerAdaptiveLearning(ids);
            });
        };
        autoTag = std::make_unique<AutoTagService>(*writeRepo);
        splitter = std::make_unique<SplitService>();
        extractor = std::make_unique<ExtractService>();
        restoringSession = true;
        if (! appSettings.lastFolder.isEmpty()
            && juce::File(appSettings.lastFolder).isDirectory()
            && readRepo->countInFolder(appSettings.lastFolder) > 0)
            selectedFolder = appSettings.lastFolder;
        folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored);
        refreshFavorites();
        reloadLibrary();
        if (! selectedFolder.isEmpty()) folders.selectPath(selectedFolder);
        if (! appSettings.lastSamplePath.isEmpty())
        {
            if (const auto record = readRepo->findByPath(appSettings.lastSamplePath))
            {
                const bool inScope = selectedFolder.isEmpty()
                    || juce::File(record->filePath).isAChildOf(juce::File(selectedFolder));
                if (inScope && library.selectSampleById(record->id))
                {
                    selectedSampleId = record->id;
                    sessionSamplePath = record->filePath;
                    updateSelectionStatus(*record);
                    updateSelectedTags();
                    if (record->isDiscEntry())
                    {
                        preview.clearDiscSource();
                        status.setText("Inside " + juce::File(record->containerPath).getFileName()
                            + "  ·  " + record->discPath + ". Press Enter to open the image.",
                            juce::dontSendNotification);
                    }
                    else if (isDiscImageFormat(record->format))
                    {
                        preview.clearDiscSource();
                        status.setText("Double-click the image or press Enter to browse its contents",
                                      juce::dontSendNotification);
                    }
                    else preview.loadFile(juce::File(record->filePath), false);
                }
            }
        }
        restoringSession = false;
        sessionDirty = false;
        status.setText(catalogCount > 0
            ? juce::String(catalogCount) + " samples loaded"
            : "Ready. Add a folder to begin.",juce::dontSendNotification);
    }
    catch (const std::exception& e) { status.setText("Database error: " + juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
    const auto missing = missingRoots();
    if (! missing.isEmpty())
    {
        status.setText(juce::String(missing.size())
            + " catalog folder(s) not found on this computer. Settings > Relocate folders...",
            juce::dontSendNotification);
        status.setTooltip(missing.joinIntoString("\n"));
    }
    else if (engine.error().isNotEmpty())
    {
        status.setText("Audio device: " + engine.error(),juce::dontSendNotification);
    }
    pause.setEnabled(false); cancel.setEnabled(false);
    addFolder.setEnabled(indexer != nullptr); rescan.setEnabled(indexer != nullptr);
    reanalyze.setEnabled(indexer != nullptr); newFiles.setEnabled(indexer != nullptr);
    smartButton.setEnabled(indexer != nullptr);
    sendLookAndFeelChange();
    addKeyListener(&enterKeyListener);
    startTimerHz(10);
    if (writeRepo != nullptr)
        featureV2Worker = std::make_unique<std::thread>([this, repo = writeRepo.get()]
        {
            backfillFeatureV2(*repo, [this] { return cancelFeatureV2.load(); });
            if (! cancelFeatureV2.load())
                AcousticClassifierService::getSharedAdaptiveClassifier().loadFromRepository(*repo);
        });
    juce::Desktop::getInstance().addDarkModeSettingListener(this);
    if (persistSessionState && ! embeddedUi)
    {
        startContribution();
        if (! contributionConsentCurrent())
        {
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe]() mutable
            {
                if (safe != nullptr) safe->askContributionConsent();
            });
        }
    }
}
MainComponent::~MainComponent()
{
    juce::PopupMenu::dismissAllActiveMenus();
    if (persistSessionState) saveSessionState();
    removeKeyListener(&enterKeyListener);
    juce::Desktop::getInstance().removeDarkModeSettingListener(this);
    stopTimer(); chooser.reset(); relocateDialog.deleteAndZero();
    embeddedSettingsPanel = nullptr;
    embeddedSettingsHost.deleteAndZero();
    settingsDialog.deleteAndZero(); audioDialog.deleteAndZero();
    if (splitDialog != nullptr)
    {
        splitDialog->setLookAndFeel(nullptr);
        closeSplitDialog();
    }
    if (extractDialog != nullptr)
    {
        extractDialog->setLookAndFeel(nullptr);
        closeExtractDialog();
    }
    if (splitter) { splitter->cancel(); splitter->waitUntilFinished(-1); }
    if (extractor) { extractor->cancel(); extractor->waitUntilFinished(-1); }
    if (contributionScheduler != nullptr)
    {
        contributionScheduler->stop();
        contributionScheduler.reset();
    }
    contributionUploader.reset();
    cancelFeedback = true;
    if (typeFeedbackWorker && typeFeedbackWorker->joinable()) typeFeedbackWorker->join();
    cancelFeatureV2 = true;
    if (featureV2Worker && featureV2Worker->joinable()) featureV2Worker->join();
    smart.reset(); indexer.reset(); tagging.reset(); autoTag.reset(); splitter.reset(); extractor.reset();
    setLookAndFeel(nullptr);
}
bool MainComponent::contains(const std::vector<int64_t>& values, int64_t id)
{
    return std::find(values.begin(), values.end(), id) != values.end();
}
void MainComponent::toggle(std::vector<int64_t>& values, int64_t id)
{
    const auto found = std::find(values.begin(), values.end(), id);
    if (found == values.end()) values.push_back(id); else values.erase(found);
}
void MainComponent::searchLibrary(const juce::String& text)
{
    pendingSearch = text;
    filter.text = text.trim();
    lastSearchChange = juce::Time::getMillisecondCounter();
    pageOffset = 0;
    reloadLibrary();
}
void MainComponent::recordSearchHistory(const juce::String& text)
{
    if (text.isEmpty()) return;
    if (! appSettings.searchHistory.contains(text))
    {
        appSettings.searchHistory.insert(0, text);
        while (appSettings.searchHistory.size() > 10) appSettings.searchHistory.remove(appSettings.searchHistory.size() - 1);
        try { settingsStore.update([this](AppSettings& saved) { saved.searchHistory = appSettings.searchHistory; }); } catch (...) {}
    }
}
void MainComponent::showSearchHistory()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    const auto history = appSettings.searchHistory;
    if (history.isEmpty()) return;
    for (int i = 0; i < history.size(); ++i) menu.addItem(i + 1, history[i]);
    menu.addSeparator(); menu.addItem(100, "Clear history");
    juce::Component::SafePointer<MainComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&search),
        [safe, history](int result)
        {
            if (safe == nullptr) return;
            if (result == 100)
            {
                safe->appSettings.searchHistory.clear();
                try { safe->settingsStore.update([](AppSettings& saved) { saved.searchHistory.clear(); }); } catch (...) {}
                if (safe->isShowing()) safe->search.grabKeyboardFocus();
                return;
            }
            if (result <= 0 || result > history.size())
            {
                if (safe->isShowing()) safe->search.grabKeyboardFocus();
                return;
            }
            safe->search.setText(history[result - 1], true);
            safe->searchLibrary(history[result - 1]);
            if (safe->isShowing()) safe->search.grabKeyboardFocus();
        });
}
void MainComponent::persistPreviewSettings()
{
    appSettings.midiPreview = preview.midiPreviewEnabled();
    appSettings.skipSilence = preview.skipSilenceEnabled();
    appSettings.autoPreview = preview.autoPreviewEnabled();
    appSettings.loopPreview = preview.loopEnabled();
    appSettings.rexGate = preview.rexGateEnabled();
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.midiPreview = appSettings.midiPreview;
            saved.skipSilence = appSettings.skipSilence;
            saved.autoPreview = appSettings.autoPreview;
            saved.loopPreview = appSettings.loopPreview;
            saved.rexGate = appSettings.rexGate;
        });
    }
    catch (const std::exception& e)
    { status.setText("Cannot save preview settings: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification); }
}
void MainComponent::persistHideEmpty()
{
    appSettings.hideEmpty = filter.hideEmpty;
    try { settingsStore.update([this](AppSettings& saved) { saved.hideEmpty = filter.hideEmpty; }); }
    catch (const std::exception& e)
    { status.setText("Cannot save Hide empty: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification); }
}
void MainComponent::persistShowIgnored()
{
    appSettings.showIgnored = filter.showIgnored;
    try { settingsStore.update([this](AppSettings& saved) { saved.showIgnored = filter.showIgnored; }); }
    catch (const std::exception& e)
    { status.setText("Cannot save Show ignored: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification); }
}
bool MainComponent::isFolderVisible(const juce::String& path) const
{
    if (readRepo == nullptr) return false;
    if (filter.showIgnored) return true;
    try { return ! isPathIgnored(path, readRepo->ignoredPaths()); }
    catch (...) { return true; }
}
void MainComponent::ensureSelectedFolderVisible()
{
    if (selectedFolder.isEmpty() || isFolderVisible(selectedFolder)) return;
    selectedFolder.clear();
    filter.folder.clear();
    folders.clearSelection();
    appSettings.lastFolder.clear();
    try { settingsStore.update([](AppSettings& saved) { saved.lastFolder.clear(); }); } catch (...) {}
}
void MainComponent::setTagFilters(std::vector<int64_t> include, std::vector<int64_t> exclude, bool matchAny)
{
    filter.includeTags = std::move(include);
    filter.excludeTags = std::move(exclude);
    filter.matchAnyTag = matchAny;
    pageOffset = 0;
    reloadLibrary();
}
void MainComponent::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Choose a sample folder");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (safe && folder.isDirectory()) safe->beginImport(folder);
        });
}
void MainComponent::beginImport(const juce::File& folder)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (indexer == nullptr) return;
    if (indexer->progress().running)
    {
        status.setText("Wait for the library scan to finish before adding a folder.",
                       juce::dontSendNotification);
        return;
    }
    try
    {
        // Adding a folder is an explicit request: a previous Remove or Ignore of this folder or
        // any ancestor no longer keeps the chosen subtree out of the scan.
        writeRepo->unignoreCoveringFolders(folder.getFullPathName());
        writeRepo->addRoot(folder.getFullPathName()); folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored); refreshFavorites();
        if (indexer->start({folder.getFullPathName()})) { wasRunning = true; reloadLibrary(); }
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
bool MainComponent::removeFolderFromLibrary(const juce::String& path)
{
    if (readRepo == nullptr || writeRepo == nullptr) return false;
    const auto target = canonicalFolderPath(path);
    if (target.isEmpty()) return false;
    if (isSplitRunning() || (indexer != nullptr && indexer->progress().running)
        || (smart != nullptr && smart->progress().running)
        || (tagging != nullptr && tagging->progress().running)
        || (autoTag != nullptr && autoTag->progress().running))
    {
        status.setText("Wait for background work to finish before removing a folder.", juce::dontSendNotification);
        return false;
    }
    try
    {
        const auto removal = writeRepo->removeFolder(target);
        auto favorites = appSettings.favoriteFolders;
        bool favoritesChanged = false;
        for (int i = favorites.size(); --i >= 0;)
        {
            const auto favorite = canonicalFolderPath(favorites[i]);
            if (favorite == target || juce::File(favorite).isAChildOf(juce::File(target)))
            { favorites.remove(i); favoritesChanged = true; }
        }
        if (favoritesChanged)
        {
            settingsStore.update([&favorites](AppSettings& saved) { saved.favoriteFolders = favorites; });
            appSettings.favoriteFolders = favorites;
        }
        auto aliases = appSettings.folderAliases;
        bool aliasesChanged = false;
        const auto aliasKeys = aliases.getAllKeys();
        for (int i = aliases.size(); --i >= 0;)
        {
            const auto aliasPath = canonicalFolderPath(aliasKeys[i]);
            if (aliasPath != target && ! juce::File(aliasPath).isAChildOf(juce::File(target))) continue;
            aliases.remove(i);
            aliasesChanged = true;
        }
        if (aliasesChanged)
        {
            settingsStore.update([&aliases](AppSettings& saved) { saved.folderAliases = aliases; });
            appSettings.folderAliases = aliases;
            folders.setFolderAliases(aliases);
            this->favorites.setFolderAliases(aliases);
        }
        if (! selectedFolder.isEmpty()
            && (canonicalFolderPath(selectedFolder) == target
                || juce::File(selectedFolder).isAChildOf(juce::File(target))))
        {
            selectedFolder.clear();
            filter.folder.clear();
        }
        if (! appSettings.lastFolder.isEmpty()
            && (canonicalFolderPath(appSettings.lastFolder) == target
                || juce::File(appSettings.lastFolder).isAChildOf(juce::File(target))))
        {
            appSettings.lastFolder.clear();
            settingsStore.update([](AppSettings& saved) { saved.lastFolder.clear(); });
        }
        if (! sessionSamplePath.isEmpty()
            && (sessionSamplePath == target || juce::File(sessionSamplePath).isAChildOf(juce::File(target))))
        {
            selectedSampleId = 0;
            sessionSamplePath.clear();
            markSessionStateDirty();
        }
        writeRepo->refreshDuplicates();
        reloadLibrary();
        folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored);
        refreshFavorites();
        ensureSelectedFolderVisible();
        status.setText("Removed " + juce::String(removal.rowsRemoved) + " catalogued file(s) from " + target
            + ". Files on disk are unchanged."
            + (removal.wasRoot ? " The folder is no longer a library root."
                               : juce::String())
            + " It stays out of scanning until you add it again.",
            juce::dontSendNotification);
        return true;
    }
    catch (const std::exception& e)
    {
        status.setText("Could not remove the folder: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return false;
    }
}
void MainComponent::requestScan(bool reanalyzeAll)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (! indexer || ! readRepo) return;
    if (indexer->progress().running)
    {
        status.setText("Wait for the library scan to finish before starting another scan.",
                       juce::dontSendNotification);
        return;
    }
    juce::StringArray roots;
    try { roots = readRepo->roots(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (roots.isEmpty()) { status.setText("Add a folder first.",juce::dontSendNotification); return; }
    const auto safe = juce::Component::SafePointer<MainComponent>(this);
    if (! reanalyzeAll)
    {
        dialogs::showAlert(*this, "Rescan library",
            "Walk every registered folder and analyze files that are new, changed or previously failed. "
            "Unchanged analyzed files are skipped.\n\nA large library can take a long time.",
            { { "Rescan", 1 }, { "Cancel", 0 } },
            [safe](int choice) { if (safe != nullptr && choice == 1) safe->scanRoots(false); },
            juce::MessageBoxIconType::QuestionIcon);
        return;
    }
    dialogs::showAlert(*this, "Reanalyze all",
        "Recompute the measured features of every file in the catalog, including files that have not changed. "
        "\n\nOn a large library this can take a long time. Completed results are saved if you cancel.",
        { { "Reanalyze", 1 }, { "Cancel", 0 } },
        [safe](int choice) { if (safe != nullptr && choice == 1) safe->scanRoots(true); },
        juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::scanRoots(bool force, bool newOnly)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (! indexer) return;
    if (indexer->progress().running)
    {
        status.setText("Wait for the library scan to finish before starting another scan.",
                       juce::dontSendNotification);
        return;
    }
    try
    {
        auto roots = readRepo->roots();
        if (roots.isEmpty()) { status.setText("Add a folder first.",juce::dontSendNotification); return; }
        const bool started = newOnly ? indexer->startNewOnly(roots) : indexer->start(roots,force);
        if (started) { wasRunning = true; reloadLibrary(); }
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
juce::File MainComponent::backupCatalog()
{
    auto* repo = writeRepo != nullptr ? writeRepo.get() : readRepo.get();
    if (repo == nullptr) throw std::runtime_error("Database is not open");
    auto folder = dbPath.getParentDirectory().getChildFile("backups");
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    auto target = folder.getChildFile("sonicator-" + stamp + ".sqlite").getNonexistentSibling();
    repo->backupTo(target);
    status.setText("Backup saved: " + target.getFullPathName(),juce::dontSendNotification);
    status.setTooltip(target.getFullPathName());
    return target;
}
juce::StringArray MainComponent::missingRoots() const
{
    juce::StringArray out;
    if (! readRepo) return out;
    try
    {
        for (const auto& root : readRepo->roots())
            if (! juce::File(root).isDirectory()) out.add(root);
    }
    catch (const std::exception&) {}
    return out;
}
void MainComponent::showRelocateDialog()
{
    if (indexer && indexer->progress().running)
    {
        status.setText("Stop indexing before relocating folders.",juce::dontSendNotification);
        return;
    }
    if (! readRepo || ! writeRepo) return;
    if (relocateDialog != nullptr) { relocateDialog->toFront(true); return; }

    const auto missing = missingRoots();
    if (missing.isEmpty())
    {
        dialogs::showAlert(*this, "Relocate folders",
            "All catalog folders are available on this computer.",
            { { "OK", 1 } }, [](int) {}, juce::MessageBoxIconType::InfoIcon);
        return;
    }

    std::vector<RelocateRootInfo> roots;
    roots.reserve((size_t) missing.size());
    for (const auto& root : missing)
    {
        int files = 0;
        try { files = readRepo->countUnderRoot(root); } catch (const std::exception&) {}
        roots.push_back({ root, files });
    }

    auto* panel = new RelocateFoldersPanel(std::move(roots));
    panel->setLookAndFeel(&look);
    panel->onProbe = [read = readRepo.get()](const juce::String& oldRoot, const juce::String& newRoot)
    {
        RelocationProbe probe;
        constexpr int limit = 200;
        int64_t lastId = 0;
        try
        {
            for (;;)
            {
                auto rows = read->pathsAfterId(lastId, 5000);
                if (rows.empty()) break;
                lastId = rows.back().id;
                for (const auto& row : rows)
                {
                    if (row.containerPath.isNotEmpty()) continue; // synthetic path, not a file
                    if (! pathrelocation::isUnder(row.path, oldRoot)) continue;
                    ++probe.sampled;
                    if (juce::File(pathrelocation::remap(row.path, oldRoot, newRoot)).existsAsFile())
                        ++probe.found;
                    if (probe.sampled >= limit) break;
                }
                if (probe.sampled >= limit) break;
            }
        }
        catch (const std::exception&) {}
        return probe;
    };
    panel->onApply = [this](const std::vector<RootRelocation>& relocations) -> juce::String
    {
        if (indexer && indexer->progress().running)
            return "Stop indexing before relocating folders.";
        if (! writeRepo || ! readRepo) return "The catalog is not open.";
        RelocationResult result;
        try
        {
            const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
            auto backup = dbPath.getSiblingFile(dbPath.getFileName() + ".relocate-" + stamp + ".sqlite")
                              .getNonexistentSibling();
            writeRepo->backupTo(backup);
            result = writeRepo->relocateRoots(relocations);

            // The transaction has committed. A failure from here on must not be reported as a
            // failed relocation, because every stored path is already relocated.
            juce::String refreshWarning;
            try { writeRepo->refreshDuplicates(); }
            catch (const std::exception& e) { refreshWarning = juce::String::fromUTF8(e.what()); }
            selectedFolder.clear(); selectedSampleId = 0; pageOffset = 0;
            try { folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored); }
            catch (const std::exception& e)
            {
                if (refreshWarning.isNotEmpty()) refreshWarning += "  |  ";
                refreshWarning += juce::String::fromUTF8(e.what());
            }
            reloadLibrary();

            juce::String message = "Relocated " + juce::String(result.movedFiles) + " file(s) in "
                + juce::String((int) relocations.size()) + " folder(s).";
            if (result.conflicts > 0)
                message += "  |  " + juce::String(result.conflicts) + " existing path collision(s) merged.";
            message += "  |  Backup: " + backup.getFullPathName();
            if (refreshWarning.isNotEmpty())
                message += "  |  Catalog refresh warning: " + refreshWarning;
            status.setText(message,juce::dontSendNotification);
            status.setTooltip(backup.getFullPathName());
            return {};
        }
        catch (const std::exception& e)
        {
            return "Relocation failed: " + juce::String::fromUTF8(e.what());
        }
    };

    auto* window = new RelocateDialogWindow();
    window->setContentOwned(panel, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    relocateDialog = window;
    window->enterModalState(true, nullptr, true);
}
bool MainComponent::refreshIfCatalogChanged()
{
    if (! readRepo) return false;
    if (pageSize == 0 && indexer != nullptr && indexer->progress().running) return false;
    try { if (readRepo->count() == totalCatalogCount) return false; }
    catch (const std::exception&) { return false; }
    reloadLibrary();
    return true;
}
void MainComponent::reloadLibrary()
{
    if (! readRepo) return;
    if (discBrowser) discBrowser->setHideEmpty(filter.hideEmpty);
    try
    {
        ensureSelectedFolderVisible();
        totalCatalogCount = readRepo->count(filter.showIgnored);
        library.setIgnoredPaths(readRepo->ignoredPaths());
        filter.folder = selectedFolder;
        int shown = 0;
        if (pageSize == 0)
        {
            auto ids = readRepo->queryIds(filter);
            catalogCount = (int) ids.size();
            shown = catalogCount;
            library.setVirtualSamples(std::move(ids), [this](const std::vector<int64_t>& pageIds)
            {
                FileListPanel::LoadedRows loaded;
                if (readRepo != nullptr)
                {
                    loaded.records = readRepo->recordsByIds(pageIds);
                    loaded.tags = readRepo->tagsForSamples(pageIds);
                    loaded.duplicates = readRepo->duplicateCounts(pageIds);
                    loaded.smart = readRepo->smartDisplays(pageIds);
                    loaded.stereoPairs = readRepo->stereoPairPartners(pageIds);
                }
                return loaded;
            });
            countLabel.setText(juce::String(catalogCount) + (filter.hasTextOrTagFilter() ? " matching" : " samples")
                + "  |  all on one page",juce::dontSendNotification);
            previousPage.setEnabled(false); nextPage.setEnabled(false);
        }
        else
        {
            auto page = readRepo->query(filter, pageSize, pageOffset);
            if (pageOffset > 0 && pageOffset >= page.total)
            {
                pageOffset = page.total > 0 ? ((page.total - 1) / pageSize) * pageSize : 0;
                page = readRepo->query(filter, pageSize, pageOffset);
            }
            catalogCount = page.total;
            shown = (int) page.samples.size();
            std::vector<int64_t> ids;
            ids.reserve(page.samples.size());
            for (const auto& sample : page.samples) ids.push_back(sample.id);
            library.setSamples(std::move(page.samples));
            library.setRowTags(readRepo->tagsForSamples(ids));
            library.setDuplicateCounts(readRepo->duplicateCounts(ids));
            library.setSmartDisplays(readRepo->smartDisplays(ids));
            library.setStereoPairs(readRepo->stereoPairPartners(ids));
            juce::String text = juce::String(catalogCount) + (filter.hasTextOrTagFilter() ? " matching" : " samples")
                + "  |  " + juce::String(shown) + " on page " + juce::String(pageOffset/pageSize+1);
            if (filter.matchAnyTag && ! filter.includeTags.empty()) text += "  |  any tag";
            countLabel.setText(text,juce::dontSendNotification);
            previousPage.setEnabled(pageOffset > 0); nextPage.setEnabled(pageOffset+pageSize < catalogCount);
        }
        allTags = readRepo->tags();
        updateTagControls();
        rootsLabel.setText(readRepo->roots().isEmpty() ? "No sample folders yet.\n\nOriginal files stay untouched."
            : "FOLDERS\n\n" + readRepo->roots().joinIntoString("\n\n"),juce::dontSendNotification);
        updateSelectedTags();
        if (selectedSampleId > 0)
        {
            if (auto selected = readRepo->findById(selectedSampleId)) updateSelectionStatus(*selected);
        }
        else selectionStatus.setText("No sample selected",juce::dontSendNotification);
        (void) shown;
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
void MainComponent::updateTagControls()
{
    const auto active = (int) (filter.includeTags.size() + filter.excludeTags.size());
    tagFilter.setButtonText(active > 0 ? "Tags (" + juce::String(active) + ")" : "Tags");
    tagFilter.setToggleState(active > 0 || filter.matchAnyTag, juce::dontSendNotification);
    starredButton.setToggleState(filter.starredOnly,juce::dontSendNotification);
    hideEmpty.setToggleState(filter.hideEmpty,juce::dontSendNotification);
    clearFilters.setEnabled(filter.hasTextOrTagFilter());
}
void MainComponent::updateSelectionStatus(const SampleRecord& r)
{
    juce::String text = r.filePath;
    if (readRepo != nullptr)
    {
        SmartDisplay d;
        try { d = readRepo->smartDisplays({ r.id })[r.id]; } catch (...) {}
        auto field = [&d](int column)
        {
            juce::String value;
            if (column == 13)
            {
                value = d.structure.isNotEmpty()
                            ? d.structure
                            : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.structureOverride == OverrideState::Value) value += " (manual)";
                else if (d.structureOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.structureOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.structureState == SmartState::Conflicting) value += " (conflict)";
                else if (d.structureState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            else if (column == 10)
            {
                value = d.type.isNotEmpty() ? d.type : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.typeOverride == OverrideState::Value) value += " (manual)";
                else if (d.typeOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.typeOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.typeState == SmartState::Conflicting) value += " (conflict)";
                else if (d.typeState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            else if (column == 11)
            {
                value = d.hasBpm ? (std::abs(d.bpm - std::round(d.bpm)) < 0.05
                                        ? juce::String((int) std::round(d.bpm))
                                        : juce::String(d.bpm, 1))
                                 : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.bpmOverride == OverrideState::Value) value += " (manual)";
                else if (d.bpmOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.bpmOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.bpmState == SmartState::Conflicting) value += " (conflict)";
                else if (d.bpmState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            else
            {
                value = d.key.isNotEmpty() ? d.key : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.keyOverride == OverrideState::Value) value += " (manual)";
                else if (d.keyOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.keyOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.keyState == SmartState::Conflicting) value += " (conflict)";
                else if (d.keyState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            if (d.pipelineState == SmartPipelineState::Stale) value += " (stale)";
            if (d.pipelineState == SmartPipelineState::Error) value += " (error)";
            return value;
        };
        text += "   |   Type: " + field(10) + "   Structure: " + field(13)
              + "   BPM: " + field(11) + "   Key: " + field(12)
              + "   Pitch: " + pitchDisplayText(d);
        if (const auto tags = readRepo->tagsForSample(r.id); ! tags.empty())
        {
            juce::StringArray names;
            for (const auto& tag : tags) names.add(tag.name);
            text += "   |   Tags: " + names.joinIntoString(", ");
        }
    }
    text += "   |   " + r.format
          + (r.sampleRate > 0 ? "  " + juce::String(r.sampleRate / 1000.0, 1) + " kHz" : juce::String())
          + (r.numChannels > 0 ? "  " + juce::String(r.numChannels) + " ch" : juce::String())
          + (r.bitsPerSample > 0 ? "  " + juce::String(r.bitsPerSample) + "-bit" : juce::String())
          + (r.duration > 0 ? "  " + juce::String(r.duration, 2) + " s" : juce::String());
    if (r.starred) text += "   |   Starred";
    if (r.missing) text += "   |   FILE MISSING";
    if (r.error.isNotEmpty()) text += "   |   " + r.error;
    selectionStatus.setText(text, juce::dontSendNotification);
    selectionStatus.setTooltip(r.filePath);
}
void MainComponent::updateSelectedTags()
{
    if (selectedSampleId <= 0 || ! readRepo)
    {
        selectedTags.setTags({});
        editTags.setEnabled(false);
        return;
    }
    selectedTags.setTags(readRepo->tagsForSample(selectedSampleId));
    editTags.setEnabled(true);
    resized();
}
void MainComponent::removeTagFromSamples(const std::vector<int64_t>& sampleIds, int64_t tagId)
{
    if (writeRepo == nullptr || sampleIds.empty() || tagId <= 0) return;
    const auto visible = writeRepo->visibleIds(sampleIds);
    if (visible.empty()) return;
    writeRepo->setSampleTag(visible, tagId, false);
    triggerAdaptiveLearning(visible);
    status.setText("Tag removed from " + juce::String(visible.size()) + " sample(s)",
                   juce::dontSendNotification);
    reloadLibrary();
}
void MainComponent::selectSample(const SampleRecord& r)
{
    const int selectedRows = library.getSelectedRowCount();
    if (selectedRows > 1)
    {
        // A batch is a library operation, not an audition: no single waveform or sound should
        // look as if it belonged to the whole selection.
        selectedSampleId = 0;
        sessionSamplePath.clear();
        markSessionStateDirty();
        preview.clearSource();
        // The selection still has a useful answer: every tag on any selected file, once.
        std::vector<int64_t> ids;
        for (const auto& sample : library.getSelectedSamples()) ids.push_back(sample.id);
        std::map<int64_t, Tag> unique;
        if (readRepo != nullptr)
            for (const auto& [sampleId, tags] : readRepo->tagsForSamples(ids))
                for (const auto& tag : tags) unique[tag.id] = tag;
        std::vector<Tag> unionTags;
        for (const auto& [id, tag] : unique) { juce::ignoreUnused(id); unionTags.push_back(tag); }
        std::sort(unionTags.begin(), unionTags.end(),
                  [](const Tag& a, const Tag& b) { return a.name.compareIgnoreCase(b.name) < 0; });
        selectedTags.setTags(unionTags);
        editTags.setEnabled(false); // editing a batch would need its own menu; display only
        resized();
        analysisDetails.setText("Multiple samples selected (" + juce::String(ids.size())
            + "). Tags on the selection: " + juce::String(unionTags.size()) + " unique."
            + (unionTags.empty() ? "" : " Alt+click a tag to remove it from every selected file."),
            juce::dontSendNotification);
        selectionStatus.setText(juce::String(selectedRows) + " samples selected",
                                juce::dontSendNotification);
        selectionStatus.setTooltip({});
        return;
    }
    selectedSampleId = r.id;
    sessionSamplePath = r.filePath;
    markSessionStateDirty();
    auto dbfs = [](double v) { return v > 0 ? juce::String(juce::Decibels::gainToDecibels(v),1) + " dBFS" : "-inf dBFS"; };
    juce::String details = r.fileName + "\n\n" + r.format + "  /  " + juce::String(r.numChannels) + " channels\n\n"
        + "Length      " + juce::String(r.duration,3) + " s\n\n"
        + "Rate          " + juce::String(r.sampleRate/1000,1) + " kHz\n\n"
        + "Bit depth   " + (r.bitsPerSample > 0 ? juce::String(r.bitsPerSample) : juce::String("N/A")) + "\n\n"
        + "Peak         " + dbfs(r.peakLevel) + "\n\nRMS         " + dbfs(r.rmsLevel) + "\n\n"
        + "Centroid   " + juce::String(r.spectralCentroid,0) + " Hz\n\n"
        + "Rolloff      " + juce::String(r.spectralRolloff,0) + " Hz\n\n"
        + "Flatness   " + juce::String(r.spectralFlatness,4);
    if (r.isDiscEntry())
        details += "\n\nInside image  " + juce::File(r.containerPath).getFileName()
            + "\nEntry         " + r.discPath
            + "\n\nDecoded on demand; the image is never extracted.";
    if (r.missing) details += "\n\nFile is missing";
    if (r.error.isNotEmpty()) details += "\n\n" + r.error;
    if (readRepo != nullptr)
    {
        if (const auto duplicate = readRepo->duplicateInfo(r.id))
        {
            details += "\n\nDuplicates   " + juce::String(duplicate->groupSize) + " copies";
            if (duplicate->representativeId != r.id)
                details += "\nShown copy is not the catalog representative.\nRepresentative: " + duplicate->representativePath;
            for (const auto& copy : readRepo->duplicateMembers(r.id))
                if (copy.id != r.id) details += "\n\n" + copy.filePath + (copy.missing ? "  (missing)" : "");
        }
    }
    analysisDetails.setText(details,juce::dontSendNotification);
    analysisDetails.setTooltip(r.filePath);
    updateSelectionStatus(r);
    updateSelectedTags();
    if (r.isDiscEntry())
    {
        ensureDiscBrowser();
        DiscEntryId parent, entry;
        parent.value = (uint64_t) r.discParent.getLargeIntValue();
        entry.value = (uint64_t) r.discEntry.getLargeIntValue();
        status.setText("Loading " + r.fileName + " from "
            + juce::File(r.containerPath).getFileName() + "...", juce::dontSendNotification);
        discBrowser->previewEntry(juce::File(r.containerPath), parent, entry, r.fileName);
        return;
    }
    if (isDiscImageFormat(r.format))
    {
        // A metadata-only image never leaves the previous source audible under a new label.
        preview.clearDiscSource();
        status.setText(r.error.isNotEmpty() ? r.error
            : "Double-click the image to browse partitions, folders and samples", juce::dontSendNotification);
        return;
    }
    if (! preview.loadFile(juce::File(r.filePath))) status.setText(engine.error(),juce::dontSendNotification);
}
void MainComponent::clearSampleSelection()
{
    library.selectRows({});
    selectedSampleId = 0;
    sessionSamplePath.clear();
    markSessionStateDirty();
    preview.clearSource();
    updateSelectedTags();
    analysisDetails.setText("Select a sample to inspect its measured audio features.",
                            juce::dontSendNotification);
    analysisDetails.setTooltip({});
    selectionStatus.setText("No sample selected", juce::dontSendNotification);
    selectionStatus.setTooltip({});
}

bool MainComponent::EnterKeyListener::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return key.getKeyCode() == juce::KeyPress::returnKey && owner.openSelectedDiscIfAny();
}

bool MainComponent::openSelectedDiscIfAny()
{
    if (discBrowser != nullptr && discBrowser->isVisible()) return false;
    const auto selected = library.getSelectedSamples();
    if (selected.empty()) return false;
    if (! selected[0].isDiscEntry() && ! isDiscImageFormat(selected[0].format)) return false;
    openDiscImage(selected[0]);
    return true;
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    // Enter opens the selected container wherever focus is; ordinary rows keep their normal
    // transport keys. This also drives the session-restored row without a mouse click.
    if (key.getKeyCode() == juce::KeyPress::returnKey && openSelectedDiscIfAny()) return true;
    return preview.keyPressed(key);
}

void MainComponent::ensureDiscBrowser()
{
    if (discBrowser) return;
    discBrowser = std::make_unique<DiscBrowserPanel>();
    discBrowser->onSelectionBegin = [this] { engine.stop(); };
    discBrowser->onImageError = [this](const juce::File& image, const juce::String& error)
    {
        folders.setDiscError(image.getFullPathName(), true);
        status.setText(error, juce::dontSendNotification);
    };
    discBrowser->onTreeReady = [this](const juce::File& image, DiscEntryId root, const std::vector<DiscEntry>& entries)
    {
        folders.setDiscFolders(image.getFullPathName(), root, entries);
    };
    discBrowser->onError = [this](const juce::String& message)
    {
        status.setText(message, juce::dontSendNotification);
    };
    discBrowser->onExit = [this]
    {
        if (discBrowser != nullptr) { discBrowser->closeImage(); discBrowser->setVisible(false); }
        library.focusForNavigation();
    };
    discBrowser->onSampleSelected = [this](const juce::File& container, const juce::String& stem,
                                           SampleAudioPtr audio)
    {
        if (! discBrowser || discBrowser->openedImage() != container) return;
        if (! discBrowser->isVisible())
        {
            // A hidden preview belongs to the library selection. Ignore a decode that finished
            // after the user selected another row (ordinary file, image or different entry).
            const auto current = selectedSampleId > 0 && readRepo != nullptr
                ? readRepo->findById(selectedSampleId) : std::nullopt;
            if (! current || ! current->isDiscEntry() || current->containerPath != container.getFullPathName()
                || current->fileName != stem) return;
        }
        if (! preview.loadDiscSample(container, stem, std::move(audio), false, 0, 0, false, true))
            status.setText(engine.error(), juce::dontSendNotification);
        else
            status.setText("Inside " + container.getFileName() + "  ·  " + stem, juce::dontSendNotification);
    };
    addChildComponent(*discBrowser);
}
void MainComponent::openDiscImage(const SampleRecord& r)
{
    if (r.isDiscEntry())
    {
        DiscEntryId parent, entry;
        parent.value = (uint64_t) r.discParent.getLargeIntValue();
        entry.value = (uint64_t) r.discEntry.getLargeIntValue();
        ensureDiscBrowser();
        discBrowser->setBounds(library.getBounds());
        discBrowser->setHideEmpty(filter.hideEmpty);
        discBrowser->setVisible(true);
        discBrowser->toFront(true);
        discBrowser->previewEntry(juce::File(r.containerPath), parent, entry, r.fileName);
        return;
    }
    if (! isDiscImageFormat(r.format)) return;
    if (isIncompatible(r.compatibility))
    {
        status.setText(r.error.isNotEmpty() ? r.error : "This image is not supported", juce::dontSendNotification);
        return;
    }
    ensureDiscBrowser();
    discBrowser->setBounds(library.getBounds());
    discBrowser->setHideEmpty(filter.hideEmpty);
    discBrowser->setVisible(true);
    discBrowser->toFront(true);
    if (discBrowser->canReuseImage(juce::File(r.filePath)))
    {
        discBrowser->refreshFolderTree();
        discBrowser->showDirectory({});
        return;
    }
    discBrowser->open(juce::File(r.filePath));
}
void MainComponent::showTagFilterMenu()
{
    if (! readRepo) return;
    try { allTags = readRepo->tags(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    juce::PopupMenu menu, excludes, manage;
    menu.setLookAndFeel(&look);
    if (allTags.empty()) menu.addSectionHeader("No tags yet");
    for (int i = 0; i < (int) allTags.size(); ++i)
    {
        const auto& tag = allTags[(size_t) i];
        menu.addItem(1000+i, tag.name + "  (" + juce::String(tag.sampleCount) + ")", true, contains(filter.includeTags, tag.id));
        excludes.addItem(2000+i, tag.name, true, contains(filter.excludeTags, tag.id));
        juce::PopupMenu actions;
        actions.addItem(4000+i*10, "Change colour...");
        actions.addItem(4000+i*10+1, "Rename...");
        if (! tag.builtin) actions.addItem(4000+i*10+2, "Delete");
        manage.addSubMenu(tag.name, actions);
    }
    menu.addSeparator();
    menu.addSubMenu("Exclude tags", excludes);
    menu.addItem(3000, "Match all selected", true, ! filter.matchAnyTag);
    menu.addItem(3001, "Match any selected", true, filter.matchAnyTag);
    menu.addSeparator();
    menu.addItem(3010, "New tag...");
    if (! allTags.empty()) menu.addSubMenu("Manage tags", manage);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tagFilter).withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<MainComponent>(this)](int result)
        {
            if (safe == nullptr || result == 0) return;
            auto& f = safe->filter;
            if (result >= 1000 && result < 2000)
            {
                const size_t index = (size_t) (result - 1000);
                if (index >= safe->allTags.size()) return;
                const auto id = safe->allTags[index].id;
                const auto excluded = std::find(f.excludeTags.begin(), f.excludeTags.end(), id);
                if (excluded != f.excludeTags.end()) f.excludeTags.erase(excluded);
                toggle(f.includeTags, id);
            }
            else if (result >= 2000 && result < 3000)
            {
                const size_t index = (size_t) (result - 2000);
                if (index >= safe->allTags.size()) return;
                const auto id = safe->allTags[index].id;
                const auto included = std::find(f.includeTags.begin(), f.includeTags.end(), id);
                if (included != f.includeTags.end()) f.includeTags.erase(included);
                toggle(f.excludeTags, id);
            }
            else if (result == 3000) f.matchAnyTag = false;
            else if (result == 3001) f.matchAnyTag = true;
            else if (result == 3010)
            {
                safe->promptTagName("New tag", {}, [safe](juce::String name)
                {
                    if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                    try
                    {
                        const auto colour = tagPalette[safe->allTags.size() % std::size(tagPalette)].colour;
                        safe->writeRepo->createTag(name, colour);
                        safe->reloadLibrary();
                    }
                    catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                });
                return;
            }
            else if (result >= 4000)
            {
                const size_t index = (size_t) (result - 4000) / 10;
                const int action = (result - 4000) % 10;
                if (index >= safe->allTags.size()) return;
                const auto tag = safe->allTags[index];
                if (action == 0) { safe->showTagColourMenu(tag.id); return; }
                if (action == 2 && ! tag.builtin)
                {
                    dialogs::showAlert(*safe, "Delete tag",
                        "Remove \"" + tag.name + "\" from every sample?",
                        { { "Delete", 1 }, { "Cancel", 0 } },
                        [safe, id = tag.id](int choice)
                        {
                            if (safe == nullptr || choice != 1) return;
                            try { safe->writeRepo->deleteTag(id); safe->reloadLibrary(); }
                            catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                        }, juce::MessageBoxIconType::WarningIcon);
                    return;
                }
                if (action == 1)
                {
                    safe->promptTagName("Rename tag", tag.name, [safe, id = tag.id](juce::String name)
                    {
                        if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                        if (! safe->writeRepo->renameTag(id, name))
                            safe->status.setText("That tag name is already in use.",juce::dontSendNotification);
                        safe->reloadLibrary();
                    });
                    return;
                }
            }
            safe->pageOffset = 0;
            safe->reloadLibrary();
        });
}
void MainComponent::showTagColourMenu(int64_t tagId)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) std::size(tagPalette); ++i) menu.addItem(i+1, tagPalette[i].name);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tagFilter),
        [safe = juce::Component::SafePointer<MainComponent>(this), tagId](int result)
        {
            if (safe == nullptr || result <= 0 || result > (int) std::size(tagPalette) || safe->writeRepo == nullptr) return;
            safe->writeRepo->setTagColour(tagId, tagPalette[result-1].colour);
            safe->reloadLibrary();
        });
}
int MainComponent::assignTagAndRemember(const std::vector<int64_t>& sampleIds, int64_t tagId)
{
    if (writeRepo == nullptr || sampleIds.empty() || tagId <= 0) return 0;
    const auto visible = writeRepo->visibleIds(sampleIds);
    if (visible.empty()) return 0;
    const int added = writeRepo->assignTag(visible, tagId);
    if (added > 0)
    {
        writeRepo->recordRecentTag(tagId);
        triggerAdaptiveLearning(visible);
    }
    return added;
}
void MainComponent::showEditTagsMenu()
{
    if (! readRepo || selectedSampleId <= 0)
        return;
    const auto sampleId = selectedSampleId;
    auto assigned = readRepo->tagsForSample(sampleId);
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) allTags.size(); ++i)
    {
        bool isAssigned = false;
        for (const auto& tag : assigned) if (tag.id == allTags[(size_t) i].id) isAssigned = true;
        menu.addItem(6000+i, allTags[(size_t) i].name, true, isAssigned);
    }
    if (allTags.empty()) menu.addSectionHeader("No tags yet");
    menu.addSeparator();
    menu.addItem(6999, "New tag...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&editTags).withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<MainComponent>(this), sampleId](int result)
        {
            if (safe == nullptr || result == 0 || safe->writeRepo == nullptr) return;
            if (result == 6999)
            {
                safe->promptTagName("New tag", {}, [safe, sampleId](juce::String name)
                {
                    if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                    try
                    {
                        const auto colour = tagPalette[safe->allTags.size() % std::size(tagPalette)].colour;
                        const auto id = safe->writeRepo->createTag(name, colour);
                        safe->assignTagAndRemember({ sampleId }, id);
                        safe->reloadLibrary();
                    }
                    catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                });
                return;
            }
            const size_t index = (size_t) (result - 6000);
            if (index >= safe->allTags.size()) return;
            const auto tag = safe->allTags[index];
            bool isAssigned = false;
            for (const auto& current : safe->readRepo->tagsForSample(sampleId))
                if (current.id == tag.id) isAssigned = true;
            if (isAssigned) { safe->writeRepo->setSampleTag(sampleId, tag.id, false); safe->triggerAdaptiveLearning({ sampleId }); }
            else safe->assignTagAndRemember({ sampleId }, tag.id);
            safe->reloadLibrary();
        });
}
void MainComponent::markSessionStateDirty()
{
    if (! restoringSession) sessionDirty = true;
}
void MainComponent::flushSessionState()
{
    saveSessionState();
}
void MainComponent::saveSessionState()
{
    if (! sessionDirty) return;
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.lastFolder = selectedFolder;
            saved.lastSamplePath = sessionSamplePath;
        });
        sessionDirty = false;
        lastSessionSaveMs = juce::Time::getMillisecondCounter();
    }
    catch (const std::exception& e)
    {
        status.setText("Session state was not saved: " + juce::String::fromUTF8(e.what()),
            juce::dontSendNotification);
    }
}
void MainComponent::refreshFavorites()
{
    if (readRepo == nullptr) return;
    const bool hasFavorites = ! appSettings.favoriteFolders.isEmpty();
    if (hasFavorites)
    {
        try { favorites.refresh(*readRepo, appSettings.favoriteFolders, false, filter.showIgnored); }
        catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
    }
    favoritesLabel.setVisible(hasFavorites);
    favorites.setVisible(hasFavorites);
    favoritesSplitter.setVisible(hasFavorites);
    if (! hasFavorites) favorites.clearSelection();
    resized();
}
bool MainComponent::isFavoriteFolder(const juce::String& path) const
{
    const auto target = canonicalFolderPath(path);
    if (target.isEmpty()) return false;
    for (const auto& favorite : appSettings.favoriteFolders)
        if (canonicalFolderPath(favorite) == target) return true;
    return false;
}
bool MainComponent::isUnderFavoriteFolder(const juce::String& path) const
{
    const juce::File target(canonicalFolderPath(path));
    if (target.getFullPathName().isEmpty()) return false;
    for (const auto& favorite : appSettings.favoriteFolders)
    {
        const juce::File root(canonicalFolderPath(favorite));
        if (target != root && target.isAChildOf(root)) return true;
    }
    return false;
}
void MainComponent::toggleFavoriteFolder(const juce::String& path)
{
    const auto target = canonicalFolderPath(path);
    if (target.isEmpty()) return;
    auto next = appSettings.favoriteFolders;
    bool removed = false;
    for (int i = next.size(); --i >= 0;)
        if (canonicalFolderPath(next[i]) == target) { next.remove(i); removed = true; }
    if (! removed)
    {
        if (isUnderFavoriteFolder(target))
        {
            status.setText("This folder is already visible inside a favorite.",juce::dontSendNotification);
            return;
        }
        // A parent favorite contains the whole subtree; drop roots that would duplicate it.
        for (int i = next.size(); --i >= 0;)
            if (juce::File(canonicalFolderPath(next[i])).isAChildOf(juce::File(target)))
                next.remove(i);
        next.add(target);
    }
    try
    {
        settingsStore.update([&next](AppSettings& saved) { saved.favoriteFolders = next; });
        appSettings.favoriteFolders = next;
        refreshFavorites();
        status.setText(removed ? "Removed from favorites: " + target
                               : "Added to favorites: " + target,juce::dontSendNotification);
    }
    catch (const std::exception& e)
    {
        status.setText("Could not save favorites: " + juce::String::fromUTF8(e.what()),juce::dontSendNotification);
    }
}
void MainComponent::showFavoriteContextMenu(const juce::String& path, juce::Point<int> screenPosition)
{
    // Only favorite roots get the menu; children are already covered by their root.
    if (path.isEmpty() || ! isFavoriteFolder(path)) return;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    menu.addItem(7201, "Remove from favorites");
    menu.addItem(7202, "Rename folder...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&favorites).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1)
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), path](int result)
        {
            if (safe == nullptr) return;
            if (result == 7201) safe->toggleFavoriteFolder(path);
            else if (result == 7202) safe->promptRenameFolder(path);
        });
}
void MainComponent::promptRenameFolder(const juce::String& path)
{
    if (readRepo == nullptr) return;
    const auto canonical = canonicalFolderPath(path);
    if (canonical.isEmpty()) return;
    const auto current = appSettings.folderAliases.getValue(
        canonical, juce::File(canonical).getFileName());
    dialogs::showPrompt(*this, "Rename folder", "Display name:", current,
        [safe = juce::Component::SafePointer<MainComponent>(this), canonical](const juce::String& value)
        {
            if (safe == nullptr) return;
            const auto name = value.trim();
            const auto original = juce::File(canonical).getFileName();
            auto aliases = safe->appSettings.folderAliases;
            if (name.isEmpty() || name == original) aliases.remove(juce::StringRef(canonical));
            else aliases.set(canonical, name);
            try
            {
                safe->settingsStore.update([aliases](AppSettings& saved) { saved.folderAliases = aliases; });
                safe->appSettings.folderAliases = aliases;
                safe->folders.setFolderAliases(aliases);
                safe->favorites.setFolderAliases(aliases);
                safe->status.setText("Folder shown as \"" + (name.isEmpty() ? original : name) + "\"",
                                     juce::dontSendNotification);
            }
            catch (const std::exception& e)
            {
                safe->status.setText("Could not save the folder name: "
                    + juce::String::fromUTF8(e.what()), juce::dontSendNotification);
            }
        });
}
void MainComponent::resetTagsSelected(const std::vector<int64_t>& sampleIds)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (readRepo == nullptr || sampleIds.empty()) return;
    if (autoTagDialog != nullptr)
    {
        status.setText("Close the AutoTag review before resetting tags.",juce::dontSendNotification);
        return;
    }
    std::vector<int64_t> visible;
    try { visible = readRepo->visibleIds(sampleIds); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (visible.empty()) { status.setText("The selected files are ignored.",juce::dontSendNotification); return; }
    int tagged = 0;
    try { tagged = readRepo->countTaggedSamples(visible); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (tagged == 0)
    {
        status.setText("The selected files have no tags to reset.",juce::dontSendNotification);
        return;
    }
    dialogs::showAlert(*this, "Reset tags",
        "Remove all tag assignments from " + juce::String(visible.size())
            + " selected file(s)?\n\n" + juce::String(tagged)
            + " file(s) currently have tags.\n\nTag definitions, stars, smart overrides and original files are kept.",
        { { "Reset", 1 }, { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<MainComponent>(this), visible](int choice)
        {
            if (safe == nullptr || choice != 1 || safe->tagging == nullptr) return;
            if (safe->autoTag != nullptr && safe->autoTag->progress().running)
            {
                safe->status.setText("Wait for AutoTag to finish before resetting tags.",juce::dontSendNotification);
                return;
            }
            if (safe->tagging->startResetSelected(visible))
                safe->status.setText("Removing tags from selected files",juce::dontSendNotification);
            else
                safe->status.setText("Tag reset could not start.",juce::dontSendNotification);
        }, juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::resetTagsFolder(const juce::String& path, int catalogued)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (readRepo == nullptr || path.isEmpty()) return;
    if (autoTagDialog != nullptr)
    {
        status.setText("Close the AutoTag review before resetting tags.",juce::dontSendNotification);
        return;
    }
    int tagged = 0;
    try { tagged = readRepo->countTaggedInFolder(path); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (tagged == 0)
    {
        status.setText("This folder has no tagged files to reset.",juce::dontSendNotification);
        return;
    }
    dialogs::showAlert(*this, "Reset tags in folder",
        "Folder:\n" + path
            + "\n\nIncludes every subfolder."
            + "\nCatalogued files: " + juce::String(catalogued)
            + "\nTagged files: " + juce::String(tagged)
            + "\n\nTag definitions, stars, smart overrides and original files are kept.",
        { { "Reset", 1 }, { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<MainComponent>(this), path](int choice)
        {
            if (safe == nullptr || choice != 1 || safe->tagging == nullptr) return;
            if (safe->autoTag != nullptr && safe->autoTag->progress().running)
            {
                safe->status.setText("Wait for AutoTag to finish before resetting tags.",juce::dontSendNotification);
                return;
            }
            if (safe->tagging->startResetFolder(path))
                safe->status.setText("Removing tags from " + path,juce::dontSendNotification);
            else
                safe->status.setText("Tag reset could not start.",juce::dontSendNotification);
        }, juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::startAutoTagSelected(const std::vector<int64_t>& sampleIds)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (autoTag == nullptr || sampleIds.empty()) return;
    std::vector<int64_t> visible;
    try { visible = readRepo == nullptr ? sampleIds : readRepo->visibleIds(sampleIds); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification); return; }
    if (visible.empty()) { status.setText("The selected files are ignored.", juce::dontSendNotification); return; }
    if (autoTagDialog != nullptr)
    {
        autoTagDialog->toFront(true);
        status.setText("Close the open AutoTag review before preparing another.", juce::dontSendNotification);
        return;
    }
    if (autoTag->progress().running || (tagging && tagging->progress().running))
    {
        status.setText("Another bulk tagging operation is already running.", juce::dontSendNotification);
        return;
    }
    if (indexer && indexer->progress().running)
    {
        status.setText("Wait for the scan to finish before AutoTag.", juce::dontSendNotification);
        return;
    }
    if (smart && smart->progress().running)
    {
        status.setText("Wait for smart analysis to finish before AutoTag.", juce::dontSendNotification);
        return;
    }
    if (autoTag->startSelected(visible))
    {
        autoTagPreparing = true;
        autoTagWasRunning = true;
        status.setText("Preparing AutoTag proposals for " + juce::String(visible.size())
            + " selected file(s)", juce::dontSendNotification);
    }
    else
        status.setText("AutoTag could not start.", juce::dontSendNotification);
}
void MainComponent::startAutoTagFolder(const juce::String& folder)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (autoTag == nullptr || folder.isEmpty()) return;
    if (autoTagDialog != nullptr)
    {
        autoTagDialog->toFront(true);
        status.setText("Close the open AutoTag review before preparing another.", juce::dontSendNotification);
        return;
    }
    if (autoTag->progress().running || (tagging && tagging->progress().running))
    {
        status.setText("Another bulk tagging operation is already running.", juce::dontSendNotification);
        return;
    }
    if (indexer && indexer->progress().running)
    {
        status.setText("Wait for the scan to finish before AutoTag.", juce::dontSendNotification);
        return;
    }
    if (smart && smart->progress().running)
    {
        status.setText("Wait for smart analysis to finish before AutoTag.", juce::dontSendNotification);
        return;
    }
    if (autoTag->startFolder(folder))
    {
        autoTagPreparing = true;
        autoTagWasRunning = true;
        status.setText("Preparing AutoTag proposals for " + folder, juce::dontSendNotification);
    }
    else
        status.setText("AutoTag could not start.", juce::dontSendNotification);
}
void MainComponent::showAutoTagDialog(AutoTagResult result)
{
    if (autoTagDialog != nullptr || autoTag == nullptr) return;
    auto* dialog = new AutoTagDialog(std::move(result), *autoTag,
        [safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->reloadLibrary();
        });
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "AutoTag proposals";
    options.dialogBackgroundColour = theme::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    autoTagDialog = options.launchAsync();
    if (autoTagDialog != nullptr) autoTagDialog->setLookAndFeel(&look);
}
void MainComponent::startSplit(const std::vector<juce::File>& files)
{
    if (files.empty() || splitter == nullptr) return;
    if (isSplitRunning())
    {
        status.setText("Split is already running.", juce::dontSendNotification);
        return;
    }
    if ((indexer && indexer->progress().running) || (smart && smart->progress().running)
        || (tagging && tagging->progress().running) || (autoTag && autoTag->progress().running)
        || isExtractRunning())
    {
        status.setText("Wait for the background job to finish before splitting.", juce::dontSendNotification);
        return;
    }
    if (splitContent != nullptr)
    {
        if (splitDialog != nullptr) splitDialog->toFront(true);
        return;
    }
    auto* dialog = new SplitDialog(files, appSettings,
        [safe = juce::Component::SafePointer<MainComponent>(this), files](SplitOptions options) -> bool
        {
            if (safe == nullptr || safe->splitter == nullptr) return false;
            safe->activeSplitOptions = options;
            safe->appSettings.splitThresholdDb = options.silence.thresholdDb;
            safe->appSettings.splitMinSilenceMs = (int) options.silence.minSilenceMs;
            safe->appSettings.splitMinPartMs = (int) options.silence.minPartMs;
            safe->appSettings.splitSameFolder = options.sameFolder;
            safe->appSettings.splitMoveToTrash = options.moveToTrash;
            try
            {
                safe->settingsStore.update([options](AppSettings& saved)
                {
                    saved.splitThresholdDb = options.silence.thresholdDb;
                    saved.splitMinSilenceMs = (int) options.silence.minSilenceMs;
                    saved.splitMinPartMs = (int) options.silence.minPartMs;
                    saved.splitSameFolder = options.sameFolder;
                    saved.splitMoveToTrash = options.moveToTrash;
                });
            }
            catch (const std::exception&)
            {
            }
            if (! safe->splitter->start(files, options)) return false;
            safe->splitStartMs = juce::Time::getMillisecondCounter();
            safe->splitWasRunning = true;
            // The batch replaces the source selection; leaving its row selected would keep the
            // old waveform visible and the preview audible under the progress report.
            safe->clearSampleSelection();
            safe->status.setText("Splitting " + juce::String((int) files.size()) + " file(s)",
                                 juce::dontSendNotification);
            return true;
        });
    dialog->onCancel = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr && safe->splitter != nullptr) safe->splitter->cancel();
    };
    dialog->onOk = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe == nullptr) return;
        if (! safe->pendingSplitScan.isEmpty())
        {
            const auto folders = safe->pendingSplitScan;
            safe->pendingSplitScan.clear();
            safe->requestSplitScan(folders);
        }
    };
    splitContent = dialog;
    auto* window = new SplitDialogWindow();
    window->setContentOwned(dialog, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->setSize(dialog->getWidth(), dialog->getHeight());
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->requestCloseSplit();
    };
    splitDialog = window;
    window->enterModalState(true, nullptr, true);
}
void MainComponent::requestSplitScan(const juce::StringArray& folders)
{
    if (folders.isEmpty()) return;
    splitScanApproved = true;
    if (indexer != nullptr && ! indexer->progress().running && ! isSplitRunning())
    {
        if (indexer->startFoldersNewOnly(folders))
        {
            wasRunning = true;
            status.setText("Scanning split parts", juce::dontSendNotification);
            return;
        }
    }
    pendingSplitScan = folders;
}
void MainComponent::closeSplitDialog()
{
    if (splitDialog != nullptr)
    {
        splitDialog->exitModalState(0);
        splitDialog = nullptr;
    }
    splitContent = nullptr;
}
void MainComponent::requestCloseSplit()
{
    if (splitContent == nullptr)
    {
        closeSplitDialog();
        return;
    }
    if (splitContent->isRunning())
    {
        if (splitter != nullptr) splitter->cancel();
        return;
    }
    if (splitContent->isFinished() && splitContent->onOk) splitContent->onOk();
    closeSplitDialog();
}
void MainComponent::startExtract(const std::vector<juce::File>& files)
{
    if (files.empty() || extractor == nullptr) return;
    if (isExtractRunning())
    {
        status.setText("Extract is already running.", juce::dontSendNotification);
        return;
    }
    if ((indexer && indexer->progress().running) || (smart && smart->progress().running)
        || (tagging && tagging->progress().running) || (autoTag && autoTag->progress().running)
        || isSplitRunning())
    {
        status.setText("Wait for the background job to finish before extracting.",
                       juce::dontSendNotification);
        return;
    }
    if (extractContent != nullptr)
    {
        if (extractDialog != nullptr) extractDialog->toFront(true);
        return;
    }
    auto* dialog = new ExtractDialog(files, appSettings,
        [safe = juce::Component::SafePointer<MainComponent>(this), files](ExtractOptions options) -> bool
        {
            if (safe == nullptr || safe->extractor == nullptr) return false;
            safe->appSettings.extractSameFolder = options.sameFolder;
            safe->appSettings.extractNormalize = options.normalize;
            safe->appSettings.extractTuneToSong = options.tuneToSong;
            if (! options.sameFolder && options.outputFolder.isDirectory())
                safe->appSettings.extractOutputFolder = options.outputFolder.getFullPathName();
            try
            {
                safe->settingsStore.update([options](AppSettings& saved)
                {
                    saved.extractSameFolder = options.sameFolder;
                    saved.extractNormalize = options.normalize;
                    saved.extractTuneToSong = options.tuneToSong;
                    if (! options.sameFolder && options.outputFolder.isDirectory())
                        saved.extractOutputFolder = options.outputFolder.getFullPathName();
                });
            }
            catch (const std::exception&)
            {
            }
            if (! safe->extractor->start(files, options)) return false;
            safe->extractStartMs = juce::Time::getMillisecondCounter();
            safe->extractWasRunning = true;
            safe->status.setText("Extracting samples from " + juce::String((int) files.size())
                + " file(s)", juce::dontSendNotification);
            return true;
        });
    dialog->onCancel = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr && safe->extractor != nullptr) safe->extractor->cancel();
    };
    extractContent = dialog;
    auto* window = new ExtractDialogWindow();
    window->setContentOwned(dialog, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->setSize(dialog->getWidth(), dialog->getHeight());
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->requestCloseExtract();
    };
    extractDialog = window;
    window->enterModalState(true, nullptr, true);
}
void MainComponent::finishExtract()
{
    if (extractor == nullptr) return;
    const auto results = extractor->results();
    const auto progress = extractor->progress();
    int filesDone = 0, filesFailed = 0;
    juce::StringArray details;
    for (const auto& result : results)
    {
        if (result.state == ExtractFileState::extracted) ++filesDone;
        else ++filesFailed;
        if (result.state == ExtractFileState::failed && result.error.isNotEmpty())
            details.add(result.source.getFileName() + ": " + result.error);
        for (const auto& problem : result.problems)
            details.add(result.source.getFileName() + ": " + problem);
    }
    if (filesFailed == 0 && details.isEmpty())
        details.add("No problems.");
    juce::String summary = "Extracted " + juce::String(progress.written) + " sample(s) from "
        + juce::String(filesDone) + " of " + juce::String((int) results.size()) + " file(s).";
    summary << "\nSkipped " << juce::String(progress.skippedEmpty) << " empty and "
            << juce::String(progress.skippedSilent) << " silent.";
    if (progress.failed > 0)
        summary << "  " << juce::String(progress.failed) << " problem(s), see details.";
    if (progress.cancelled)
        summary << "\nCancelled; finished samples were kept.";
    status.setText(summary.replace("\n", " "), juce::dontSendNotification);
    status.setTooltip(details.joinIntoString("\n"));
    if (extractContent != nullptr)
        extractContent->showFinished(summary, details.joinIntoString("\n"),
            progress.cancelled ? progress.fraction : 1.0);
}
void MainComponent::closeExtractDialog()
{
    if (extractDialog != nullptr)
    {
        extractDialog->exitModalState(0);
        extractDialog = nullptr;
    }
    extractContent = nullptr;
}
void MainComponent::requestCloseExtract()
{
    if (extractContent == nullptr)
    {
        closeExtractDialog();
        return;
    }
    if (extractContent->isRunning())
    {
        if (extractor != nullptr) extractor->cancel();
        return;
    }
    closeExtractDialog();
}
void MainComponent::finishSplit()
{
    if (splitter == nullptr || readRepo == nullptr) return;
    const auto results = splitter->results();
    const auto progress = splitter->progress();
    const bool batchCancelled = progress.cancelled;
    int splitCount = 0, partCount = 0, failedCount = 0, skippedCount = 0;
    int noSplitCount = 0, cancelledCount = 0, droppedCount = 0;
    juce::StringArray outputFolders, problems;
    std::vector<SplitFileResult> deletable;
    for (const auto& result : results)
    {
        partCount += (int) result.parts.size();
        for (const auto& part : result.parts)
        {
            const auto folder = part.getParentDirectory().getFullPathName();
            if (! outputFolders.contains(folder)) outputFolders.add(folder);
        }
        switch (result.state)
        {
            case SplitFileState::split: ++splitCount; break;
            case SplitFileState::failed: ++failedCount; break;
            case SplitFileState::skipped: ++skippedCount; break;
            case SplitFileState::noSplit: ++noSplitCount; break;
            case SplitFileState::cancelled: ++cancelledCount; break;
        }
        if (result.discardedShortParts > 0)
        {
            ++droppedCount;
            problems.add(result.source.getFileName() + ": "
                + juce::String(result.discardedShortParts) + " short fragment(s) dropped");
        }
        if (result.state != SplitFileState::split)
            problems.add(result.source.getFileName()
                + (result.error.isNotEmpty() ? ": " + result.error : juce::String()));
        if (deletionAllowed(result, batchCancelled)) deletable.push_back(result);
    }

    juce::StringArray moved, moveFailed;
    if (activeSplitOptions.moveToTrash && ! batchCancelled)
    {
        for (const auto& result : deletable)
        {
            const auto source = result.source;
            if (! source.existsAsFile())
            {
                moveFailed.add(source.getFileName());
                continue;
            }
            const auto sidecars = splitSidecarPaths(source);
            if (! source.moveToTrash())
            {
                moveFailed.add(source.getFileName());
                continue;
            }
            moved.add(source.getFullPathName());
            for (const auto& sidecarPath : sidecars)
            {
                juce::File sidecar(sidecarPath);
                if (sidecar.existsAsFile() && ! sidecar.moveToTrash())
                    moveFailed.add(sidecar.getFileName());
            }
        }
        if (! moved.isEmpty())
        {
            try
            {
                writeRepo->removeByPaths(moved);
                writeRepo->refreshDuplicates();
                reloadLibrary();
                if (readRepo != nullptr)
                    folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored);
                refreshFavorites();
            }
            catch (const std::exception& e)
            {
                problems.add(juce::String::fromUTF8(e.what()));
            }
        }
        if (! moveFailed.isEmpty())
            problems.add("Could not move to Trash: " + moveFailed.joinIntoString(", "));
    }

    juce::String summary = "Split: " + juce::String(splitCount) + " file(s), "
        + juce::String(partCount) + " part(s)";
    if (failedCount > 0) summary += "  |  failed " + juce::String(failedCount);
    if (skippedCount > 0) summary += "  |  skipped " + juce::String(skippedCount);
    if (noSplitCount > 0) summary += "  |  no silence " + juce::String(noSplitCount);
    if (cancelledCount > 0) summary += "  |  cancelled " + juce::String(cancelledCount);
    if (droppedCount > 0) summary += "  |  dropped fragments " + juce::String(droppedCount);
    if (batchCancelled) summary += "  |  cancelled";
    if (! moved.isEmpty()) summary += "  |  moved to Trash " + juce::String(moved.size());
    status.setText(summary, juce::dontSendNotification);
    status.setTooltip(summary + (problems.isEmpty() ? juce::String()
                                                    : "\n" + problems.joinIntoString("\n")));
    pendingSplitScan = outputFolders;
    splitScanApproved = false;

    if (splitContent != nullptr)
    {
        const double fraction = progress.total > 0
            ? juce::jlimit(0.0, 1.0, (double) progress.processed / (double) progress.total) : 1.0;
        splitContent->showFinished(summary + (problems.isEmpty() ? juce::String()
                                                                 : "\n\n" + problems.joinIntoString("\n")),
                                   fraction);
    }
    else if (! pendingSplitScan.isEmpty())
    {
        const auto queued = pendingSplitScan;
        pendingSplitScan.clear();
        requestSplitScan(queued);
    }
}
void MainComponent::showFolderContextMenu(const juce::String& path, bool allSamples,
                                          juce::Point<int> screenPosition)
{
    // All samples is a virtual node: no filesystem open and no implicit whole-catalog bulk work.
    if (allSamples || path.isEmpty() || readRepo == nullptr) return;
    const juce::File folder(path);
    const bool available = folder.isDirectory();
    int catalogued = 0;
    try
    {
        catalogued = readRepo->countInFolder(path);
        allTags = readRepo->tags();
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
#if JUCE_MAC
    const juce::String openLabel = "Open in Finder";
#elif JUCE_WINDOWS
    const juce::String openLabel = "Open in Explorer";
#else
    const juce::String openLabel = "Open folder";
#endif
    juce::PopupMenu menu, tagMenu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) allTags.size(); ++i)
        tagMenu.addItem(7100 + i, allTags[(size_t) i].name, true, false,
                        tagDotImage(allTags[(size_t) i].colour));
    if (allTags.empty()) tagMenu.addSectionHeader("No tags yet");
    const bool exactFavorite = isFavoriteFolder(path);
    const bool underFavorite = isUnderFavoriteFolder(path);
    const juce::String favoriteLabel = exactFavorite ? "Remove from favorites"
        : (underFavorite ? "Already in favorites" : "Add to favorites");
    menu.addItem(7001, openLabel, available);
    menu.addItem(7006, favoriteLabel, exactFavorite || ! underFavorite);
    const bool exactIgnore = readRepo->hasExactIgnore(path);
    const bool inheritedIgnore = ! exactIgnore && readRepo->isIgnored(path);
    menu.addItem(7007, inheritedIgnore ? "Ignored with parent folder" : "Ignore",
                 ! inheritedIgnore, exactIgnore);
    // Only a registered root gets a display name; subfolders keep the real folder name.
    const bool isRoot = readRepo->roots().contains(path);
    if (isRoot) menu.addItem(7011, "Rename folder...");
    menu.addSeparator();
    const bool splitBusy = isSplitRunning();
    menu.addItem(7002, "Scan folder", indexer != nullptr && ! indexer->progress().running && ! splitBusy);
    menu.addItem(7003, "Analyze smart metadata (" + juce::String(catalogued) + ")",
                 smart != nullptr && ! smart->progress().running && ! splitBusy
                     && (indexer == nullptr || ! indexer->progress().running));
    const bool autoTagIdle = autoTag != nullptr && ! autoTag->progress().running
        && (indexer == nullptr || ! indexer->progress().running)
        && (smart == nullptr || ! smart->progress().running)
        && (tagging == nullptr || ! tagging->progress().running)
        && ! splitBusy;
    menu.addItem(7004, "AutoTag folder...", autoTagIdle && catalogued > 0);
    menu.addSeparator();
    menu.addSubMenu("Tag folder...", tagMenu, ! allTags.empty() && catalogued > 0 && ! splitBusy);
    menu.addItem(7005, "Reset tags...", catalogued > 0 && autoTagIdle && autoTagDialog == nullptr);
    const bool removable = available || readRepo->roots().contains(path);
    if (removable)
    {
        menu.addSeparator();
        menu.addItem(7008, "Remove from library...", autoTagIdle);
    }
    const juce::String folderPath = path;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&folders).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1)
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), folderPath, catalogued, exactIgnore](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (safe->isSplitRunning() && result != 7001 && result != 7006 && result != 7007
                && result != 7011)
            {
                safe->status.setText("Wait for Split to finish.", juce::dontSendNotification);
                return;
            }
            try
            {
                if (result == 7011)
                {
                    safe->promptRenameFolder(folderPath);
                }
                else if (result == 7007)
                {
                    if (safe->writeRepo == nullptr) return;
                    if (exactIgnore) safe->writeRepo->unignorePath(folderPath);
                    else safe->writeRepo->ignorePath(folderPath, true);
                    safe->ensureSelectedFolderVisible();
                    safe->reloadLibrary();
                    safe->folders.refresh(*safe->readRepo, safe->readRepo->roots(), true,
                                          safe->filter.showIgnored);
                    safe->refreshFavorites();
                    safe->status.setText(exactIgnore ? "Ignoring removed for " + folderPath
                                                     : "Ignoring " + folderPath,
                                         juce::dontSendNotification);
                }
                if (result >= 7100)
                {
                    const size_t index = (size_t) (result - 7100);
                    if (index >= safe->allTags.size() || safe->readRepo == nullptr) return;
                    const auto tag = safe->allTags[index];
                    const int already = safe->readRepo->countWithTagInFolder(folderPath, tag.id);
                    const int added = juce::jmax(0, catalogued - already);
                    dialogs::showAlert(*safe, "Tag folder",
                        "Folder:\n" + folderPath
                            + "\n\nIncludes every subfolder."
                            + "\nCatalogued files: " + juce::String(catalogued)
                            + "\nNew assignments: " + juce::String(added)
                            + "\nAlready tagged: " + juce::String(already)
                            + "\n\nOriginal files are not changed. Repeating this is safe.",
                        { { "Tag", 1 }, { "Cancel", 0 } },
                        [safe, folderPath, tag](int choice)
                        {
                            if (safe == nullptr || choice != 1 || safe->tagging == nullptr) return;
                            if (safe->tagging->progress().running)
                            {
                                safe->status.setText("Another tagging operation is already running.", juce::dontSendNotification);
                                return;
                            }
                            if (safe->autoTag != nullptr && safe->autoTag->progress().running)
                            {
                                safe->status.setText("Wait for AutoTag to finish before tagging the folder.", juce::dontSendNotification);
                                return;
                            }
                            if (safe->tagging->startTagFolder(folderPath, tag.id))
                                safe->status.setText("Tagging catalogued files in " + folderPath, juce::dontSendNotification);
                            else
                                safe->status.setText("Tagging could not start.", juce::dontSendNotification);
                        }, juce::MessageBoxIconType::QuestionIcon);
                }
                else if (result == 7006)
                {
                    safe->toggleFavoriteFolder(folderPath);
                }
                else if (result == 7005)
                {
                    safe->resetTagsFolder(folderPath, catalogued);
                }
                else if (result == 7001)
                {
                    const juce::File target(folderPath);
                    if (! target.isDirectory())
                        safe->status.setText("Folder is not available: " + folderPath, juce::dontSendNotification);
                    else if (! juce::Process::openDocument(folderPath, {}))
                        safe->status.setText("Could not open " + folderPath, juce::dontSendNotification);
                }
                else if (result == 7002)
                {
                    if (safe->indexer == nullptr || safe->indexer->progress().running)
                    {
                        safe->status.setText("Another scan is already running.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->smart != nullptr && safe->smart->progress().running)
                    {
                        safe->status.setText("Wait for smart analysis to finish before scanning.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->indexer->startSubtree(folderPath))
                        safe->status.setText("Scanning " + folderPath, juce::dontSendNotification);
                    else
                        safe->status.setText("Scan could not start.", juce::dontSendNotification);
                }
                else if (result == 7003)
                {
                    if (safe->smart == nullptr || safe->smart->progress().running)
                    {
                        safe->status.setText("Smart analysis is already running.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->indexer != nullptr && safe->indexer->progress().running)
                    {
                        safe->status.setText("Wait for the scan to finish before analyzing metadata.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->smart->startFolder(folderPath))
                        safe->status.setText("Analyzing metadata for " + juce::String(catalogued)
                            + " catalogued file(s)", juce::dontSendNotification);
                    else
                        safe->status.setText("Smart analysis could not start.", juce::dontSendNotification);
                }
                else if (result == 7004)
                {
                    safe->startAutoTagFolder(folderPath);
                }
                else if (result == 7008)
                {
                    dialogs::showAlert(*safe, "Remove folder from library",
                        "Folder:\n" + folderPath
                            + "\n\nCatalogued files: " + juce::String(catalogued)
                            + "\n\nTheir catalog rows, tags and analysis are removed from this catalog."
                            + "\nFiles on disk are not changed."
                            + (safe->readRepo->roots().contains(folderPath)
                                   ? "\n\nThe folder is also removed from the library roots."
                                   : "\n\nA later scan of the parent folder will not bring it back."),
                        { { "Remove", 1 }, { "Cancel", 0 } },
                        [safe, folderPath](int choice)
                        {
                            if (safe != nullptr && choice == 1) safe->removeFolderFromLibrary(folderPath);
                        }, juce::MessageBoxIconType::WarningIcon);
                }
            }
            catch (const std::exception& e)
            {
                safe->status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification);
            }
        });
}
void MainComponent::showSampleContextMenu(const std::vector<SampleRecord>& selection,
                                          const SampleRecord& lead,
                                          juce::Point<int> screenPosition)
{
    if (selection.empty() || readRepo == nullptr) return;
    std::vector<IgnoredPath> ignoredPaths;
    try { allTags = readRepo->tags(); ignoredPaths = readRepo->ignoredPaths(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    std::vector<int64_t> ids;
    bool allStarred = true, anyExisting = false;
    for (const auto& record : selection)
    {
        ids.push_back(record.id);
        allStarred = allStarred && record.starred;
        anyExisting = anyExisting || juce::File(record.filePath).existsAsFile();
    }
    bool allIgnored = true, anyInherited = false;
    for (const auto& record : selection)
    {
        const bool ignored = isPathIgnored(record.filePath, ignoredPaths);
        allIgnored = allIgnored && ignored;
        if (ignored && ! readRepo->hasExactIgnore(record.filePath)) anyInherited = true;
    }
    std::vector<juce::File> existingFiles, splitCandidates, extractCandidates;
    for (const auto& record : selection)
    {
        const juce::File file(record.filePath);
        if (! file.existsAsFile() || isPathIgnored(record.filePath, ignoredPaths)) continue;
        if (! FileScanner::isSupportedFormat(file)) continue;
        existingFiles.push_back(file);
        if (! FileScanner::isMetadataOnlyFormat(file)) splitCandidates.push_back(file);
    }
    // Embedded sources are metadata-only for scanning, so they must be selected from the
    // existing-file list rather than from the split candidates.
    extractCandidates = embeddedSources(existingFiles);
    const auto assignments = readRepo->tagsForSamples(ids);
    std::map<int64_t, bool> allHave;
    for (const auto& tag : allTags)
    {
        bool every = true;
        for (auto id : ids)
        {
            bool has = false;
            const auto found = assignments.find(id);
            if (found != assignments.end())
                for (const auto& assigned : found->second)
                    if (assigned.id == tag.id) { has = true; break; }
            if (! has) { every = false; break; }
        }
        allHave[tag.id] = every;
    }

    auto recent = readRepo->recentTags();
    juce::PopupMenu menu, tagsMenu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) recent.size(); ++i)
        menu.addItem(20000 + i, recent[(size_t) i].name, true, false,
                     tagDotImage(recent[(size_t) i].colour));
    if (! recent.empty()) menu.addSeparator();
    menu.addItem(9000, "Copy", anyExisting);
    menu.addItem(9009, anyInherited ? "Ignored with folder" : "Ignore",
                 ! anyInherited, allIgnored);
    menu.addItem(9008, splitCandidates.size() > 1
        ? "Split by silence (" + juce::String((int) splitCandidates.size()) + ")..."
        : juce::String("Split by silence..."), ! splitCandidates.empty());
    menu.addItem(9010, extractCandidates.size() > 1
        ? "Extract samples from " + juce::String((int) extractCandidates.size()) + " files..."
        : juce::String("Extract samples..."), ! extractCandidates.empty());
    for (int i = 0; i < (int) allTags.size(); ++i)
        tagsMenu.addItem(10000 + i, allTags[(size_t) i].name, true,
                         allHave[allTags[(size_t) i].id], tagDotImage(allTags[(size_t) i].colour));
    if (! allTags.empty()) tagsMenu.addSeparator();
    tagsMenu.addItem(9003, "New tag...");
    menu.addSubMenu("Tag", tagsMenu);

    juce::PopupMenu typeMenu;
    const auto standardTypes = standardSampleTypeNames();
    juce::String commonType;
    bool hasFirst = false;
    bool uniformType = true;
    bool anyTypeOverride = false;
    const auto displays = readRepo->smartDisplays(ids);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        const auto it = displays.find(ids[i]);
        if (it != displays.end())
        {
            if (it->second.typeOverride != OverrideState::None)
                anyTypeOverride = true;
            if (! hasFirst)
            {
                commonType = it->second.type;
                hasFirst = true;
            }
            else if (! commonType.equalsIgnoreCase(it->second.type))
                uniformType = false;
        }
    }

    for (int i = 0; i < standardTypes.size(); ++i)
    {
        const auto& t = standardTypes[i];
        const bool isChecked = uniformType && commonType.equalsIgnoreCase(t);
        typeMenu.addItem(30000 + i, t, true, isChecked);
    }

    auto customList = readRepo->customTypes();
    std::sort(customList.begin(), customList.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareIgnoreCase(b) < 0;
    });
    if (! customList.empty())
    {
        typeMenu.addSeparator();
        for (int i = 0; i < (int) customList.size(); ++i)
        {
            const auto& t = customList[(size_t) i];
            const bool isChecked = uniformType && commonType.equalsIgnoreCase(t);
            typeMenu.addItem(40000 + i, t, true, isChecked);
        }
    }

    typeMenu.addSeparator();
    typeMenu.addItem(9021, "+ Custom type...");
    typeMenu.addItem(9020, "Reset type override", anyTypeOverride);
    menu.addSubMenu("Type", typeMenu);

    menu.addItem(9001, allStarred ? "Unstar" : "Star", true, false, starMenuImage());
    const bool smartAvailable = smart != nullptr && ! (indexer && indexer->progress().running);
    menu.addItem(9005, selection.size() > 1 ? "Analyze selected (" + juce::String(selection.size()) + ")"
                                            : juce::String("Analyze selected"),
                 smartAvailable);
    const bool autoTagIdle = autoTag != nullptr && ! autoTag->progress().running
        && ! (indexer && indexer->progress().running)
        && ! (smart && smart->progress().running)
        && (tagging == nullptr || ! tagging->progress().running);
    menu.addItem(9006, "AutoTag selected...", autoTagIdle);
    const bool resetIdle = ! ids.empty() && autoTagDialog == nullptr
        && (tagging == nullptr || ! tagging->progress().running)
        && (autoTag == nullptr || ! autoTag->progress().running);
    menu.addItem(9007, "Reset tags...", resetIdle);
#if JUCE_MAC
    const juce::String openLabel = "Open in Finder";
#elif JUCE_WINDOWS
    const juce::String openLabel = "Open in Explorer";
#else
    const juce::String openLabel = "Open folder";
#endif
    const juce::File leadFile(lead.filePath);
    menu.addItem(9002, openLabel, leadFile.existsAsFile() || leadFile.getParentDirectory().isDirectory());
    const auto duplicate = readRepo->duplicateInfo(lead.id);
    if (duplicate.has_value() && duplicate->groupSize > 1)
        menu.addItem(9004, "Show all " + juce::String(duplicate->groupSize) + " copies");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&library).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1)
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), ids, allStarred, allHave, recent,
         files = selection, leadPath = lead.filePath, leadHash = lead.fileHash,
         ignoredPaths, allIgnored, anyInherited, splitCandidates, extractCandidates, customList,
         standardTypes](int result)
        {
            if (safe == nullptr || result == 0) return;
            if ((safe->isSplitRunning() || safe->isExtractRunning())
                && result != 9000 && result != 9002 && result != 9004)
            {
                safe->status.setText("Wait for the running job to finish.", juce::dontSendNotification);
                return;
            }
            try
            {
                if (result == 9020)
                {
                    safe->cancelFeedback = true;
                    if (safe->typeFeedbackWorker && safe->typeFeedbackWorker->joinable())
                        safe->typeFeedbackWorker->join();
                    safe->cancelFeedback = false;
                    for (auto id : ids)
                    {
                        safe->writeRepo->clearSmartOverride(id, SmartField::Type);
                        safe->writeRepo->removeFeedback(id);
                        AcousticClassifierService::getSharedAdaptiveClassifier().removePrototype(id);
                    }
                    safe->reloadLibrary();
                    safe->status.setText("Reset type override for " + juce::String(ids.size()) + " sample(s)", juce::dontSendNotification);
                }
                else if (result == 9021)
                {
                    safe->promptCustomType([safe, ids, files, standardTypes](juce::String name)
                    {
                        if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                        name = name.trim();
                        for (const auto& st : standardTypes)
                        {
                            if (st.equalsIgnoreCase(name))
                            {
                                safe->applyTypeOverride(ids, files, st);
                                return;
                            }
                        }
                        safe->writeRepo->addCustomType(name);
                        safe->applyTypeOverride(ids, files, name);
                    });
                }
                else if (result >= 40000)
                {
                    const size_t index = (size_t) (result - 40000);
                    if (index < customList.size())
                        safe->applyTypeOverride(ids, files, customList[index]);
                }
                else if (result >= 30000)
                {
                    const size_t index = (size_t) (result - 30000);
                    if (index < (size_t) standardTypes.size())
                        safe->applyTypeOverride(ids, files, standardTypes[(int) index]);
                }
                else if (result >= 20000)
                {
                    const size_t index = (size_t) (result - 20000);
                    if (index >= recent.size()) return;
                    const auto tag = recent[index];
                    const int added = safe->assignTagAndRemember(ids, tag.id);
                    safe->status.setText(added > 0
                        ? "Tagged " + juce::String(added) + " of " + juce::String(ids.size())
                            + " selected file(s) as " + tag.name
                        : "All selected files already have the tag " + tag.name,
                        juce::dontSendNotification);
                    safe->reloadLibrary();
                }
                else if (result == 9000)
                {
                    juce::Array<juce::File> selected;
                    for (const auto& record : files)
                        if (juce::File(record.filePath).existsAsFile()) selected.add(juce::File(record.filePath));
                    if (selected.isEmpty()) { safe->status.setText("No existing files to copy.",juce::dontSendNotification); return; }
                    const bool native = copyFilesToClipboard(selected);
                    safe->status.setText(native ? "Copied " + juce::String(selected.size()) + " file(s)"
                                                : "Copied " + juce::String(selected.size()) + " path(s)",juce::dontSendNotification);
                }
                else if (result == 9008)
                {
                    safe->startSplit(splitCandidates);
                }
                else if (result == 9010)
                {
                    safe->startExtract(extractCandidates);
                }
                else if (result == 9009)
                {
                    int changed = 0;
                    if (allIgnored)
                    {
                        std::vector<juce::String> pathsToUnignore;
                        for (const auto& record : files)
                            if (safe->readRepo->hasExactIgnore(record.filePath))
                                pathsToUnignore.push_back(record.filePath);
                        changed = safe->writeRepo->unignorePaths(pathsToUnignore);
                    }
                    else
                    {
                        std::vector<juce::String> pathsToIgnore;
                        for (const auto& record : files)
                            if (! isPathIgnored(record.filePath, ignoredPaths))
                                pathsToIgnore.push_back(record.filePath);
                        changed = safe->writeRepo->ignorePaths(pathsToIgnore, false);
                    }
                    const auto updatedIgnored = safe->readRepo->ignoredPaths();
                    safe->library.setIgnoredPaths(updatedIgnored);
                    safe->folders.setIgnoredPaths(updatedIgnored, safe->filter.showIgnored);
                    safe->favorites.setIgnoredPaths(updatedIgnored, safe->filter.showIgnored);
                    safe->ensureSelectedFolderVisible();
                    safe->reloadLibrary();
                    safe->status.setText((allIgnored ? "Stopped ignoring " : "Ignoring ")
                        + juce::String(changed) + " file(s)", juce::dontSendNotification);
                }
                else if (result == 9001)
                {
                    safe->writeRepo->setStarred(ids, ! allStarred);
                    if (safe->filter.starredOnly)
                    {
                        safe->reloadLibrary();
                    }
                    else
                    {
                        safe->library.setStarredInPlace(ids, ! allStarred);
                        if (safe->selectedSampleId > 0 && std::find(ids.begin(), ids.end(), safe->selectedSampleId) != ids.end())
                        {
                            if (auto selected = safe->readRepo->findById(safe->selectedSampleId))
                                safe->updateSelectionStatus(*selected);
                        }
                    }
                }
                else if (result == 9002)
                {
                    const juce::File file(leadPath);
                    if (file.existsAsFile()) file.revealToUser();
                    else if (file.getParentDirectory().isDirectory()) file.getParentDirectory().revealToUser();
                    else safe->status.setText("Folder is not available.",juce::dontSendNotification);
                }
                else if (result == 9005)
                {
                    if (safe->smart == nullptr) return;
                    std::vector<int64_t> selectedIds;
                    for (const auto& record : files) selectedIds.push_back(record.id);
                    if (! selectedIds.empty() && safe->smart->progress().running)
                    {
                        int queued = 0;
                        for (auto id : selectedIds) if (safe->smart->requestSample(id)) ++queued;
                        safe->status.setText("Queued " + juce::String(queued)
                            + " sample(s) for smart analysis", juce::dontSendNotification);
                    }
                    else if (safe->smart->startSelected(selectedIds))
                        safe->status.setText("Analyzing " + juce::String(selectedIds.size())
                            + " selected sample(s)", juce::dontSendNotification);
                    else
                        safe->status.setText("Smart analysis could not start", juce::dontSendNotification);
                }
                else if (result == 9006)
                {
                    std::vector<int64_t> selectedIds;
                    for (const auto& record : files) selectedIds.push_back(record.id);
                    safe->startAutoTagSelected(selectedIds);
                }
                else if (result == 9007)
                {
                    safe->resetTagsSelected(ids);
                }
                else if (result == 9004)
                {
                    if (leadHash.isEmpty()) return;
                    safe->filter.duplicateHash = leadHash;
                    safe->filter.hideDuplicates = false;
                    safe->filter.text.clear(); safe->filter.includeTags.clear();
                    safe->filter.excludeTags.clear(); safe->filter.matchAnyTag = false;
                    safe->filter.starredOnly = false; safe->selectedFolder.clear();
                    safe->search.clear(); safe->pendingSearch.clear();
                    safe->folders.clearSelection();
                    safe->pageOffset = 0;
                    safe->reloadLibrary();
                }
                else if (result == 9003)
                {
                    safe->promptTagName("New tag", {}, [safe, ids](juce::String name)
                    {
                        if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                        try
                        {
                            const auto colour = tagPalette[safe->allTags.size() % std::size(tagPalette)].colour;
                            const auto id = safe->writeRepo->createTag(name, colour);
                            safe->assignTagAndRemember(ids, id);
                            safe->reloadLibrary();
                        }
                        catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                    });
                }
                else if (result >= 10000)
                {
                    const size_t index = (size_t) (result - 10000);
                    if (index >= safe->allTags.size()) return;
                    const auto tag = safe->allTags[index];
                    const auto state = allHave.find(tag.id);
                    const bool assign = state == allHave.end() || ! state->second;
                    if (assign) safe->assignTagAndRemember(ids, tag.id);
                    else
                    {
                        safe->writeRepo->setSampleTag(ids, tag.id, false);
                        safe->triggerAdaptiveLearning(ids);
                    }
                    safe->reloadLibrary();
                }
            }
            catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
        });
}
void MainComponent::promptTagName(const juce::String& title, const juce::String& initial,
                                  std::function<void(juce::String)> onAccept)
{
    dialogs::showPrompt(*this, title, "Tag name:", initial, std::move(onAccept));
}
void MainComponent::promptCustomType(std::function<void(juce::String)> onAccept)
{
    dialogs::showPrompt(*this, "New custom type", "Type name:", {}, std::move(onAccept));
}
void MainComponent::applyTypeOverride(const std::vector<int64_t>& ids,
                                      const std::vector<SampleRecord>& files,
                                      const juce::String& type)
{
    if (ids.empty() || writeRepo == nullptr) return;
    SmartOverride val;
    val.textValue = type;
    for (auto id : ids)
    {
        writeRepo->setSmartOverride(id, SmartField::Type, val);
    }
    reloadLibrary();
    status.setText("Set type to " + type + " for " + juce::String(ids.size()) + " sample(s)", juce::dontSendNotification);

    triggerAdaptiveLearning(ids);
    juce::ignoreUnused(files);
}
void MainComponent::triggerAdaptiveLearning(const std::vector<int64_t>& ids)
{
    if (ids.empty() || writeRepo == nullptr) return;

    if (contributionScheduler != nullptr)
    {
        contributionScheduler->stop();
        contributionScheduler.reset();
    }
    contributionUploader.reset();
    cancelFeedback = true;
    if (typeFeedbackWorker && typeFeedbackWorker->joinable())
        typeFeedbackWorker->join();
    cancelFeedback = false;

    typeFeedbackWorker = std::make_unique<std::thread>([this, sampleIds = ids, repo = writeRepo.get()]()
    {
        if (repo == nullptr) return;
        for (auto id : sampleIds)
        {
            if (cancelFeedback.load()) break;
            const auto record = repo->findById(id);
            if (! record) continue;
            const juce::File f(record->filePath);
            if (! f.existsAsFile()) continue;

            std::vector<float> features;
            int featureSchema = 0;
            for (const auto& fb : repo->allFeedback())
                if (fb.sampleId == id)
                {
                    features = fb.features;
                    featureSchema = fb.featureSchema;
                    break;
                }
            const bool needV1 = features.empty();
            const bool needV2 = featureSchema < 2;
            AnalysisAudio audio;
            if (needV1 || needV2)
            {
                AnalysisAudioOptions opts;
                opts.maxSeconds = 30.0;
                audio = readAnalysisAudio(f, opts);
                if (needV1 && ! audio.ok()) continue;
            }
            if (needV1)
            {
                features = AdaptiveClassifier::extractFeatures(audio.buffer, audio.sampleRate);
                if (features.empty()) continue;
            }
            if (cancelFeedback.load()) break;

            const auto sampleTags = repo->tagsForSample(id);
            juce::StringArray tagNames;
            for (const auto& t : sampleTags) tagNames.add(t.name);

            juce::String type;
            const auto currentOverrides = repo->smartOverrides(id);
            const auto it = currentOverrides.find(SmartField::Type);
            if (it != currentOverrides.end() && ! it->second.cleared && ! it->second.notApplicable)
                type = it->second.textValue;
            else if (const auto res = repo->smartResult(id))
                type = res->type;

            if (tagNames.isEmpty() && type.isEmpty())
            {
                repo->removeFeedback(id);
                AcousticClassifierService::getSharedAdaptiveClassifier().removePrototype(id);
            }
            else
            {
                repo->recordFeedback(id, type, tagNames, features,
                                     SampleRepository::FeedbackOrigin::user);
                std::vector<float> featuresV2;
                if (needV2 && audio.ok())
                {
                    const auto vector = FeatureExtractorV2::extract(audio.buffer, audio.sampleRate);
                    if (vector.ok())
                    {
                        featuresV2 = vector.values;
                        repo->setFeedbackV2(id, featuresV2);
                    }
                }
                AcousticClassifierService::getSharedAdaptiveClassifier().updatePrototype(
                    id, type, tagNames, features, featuresV2);
            }
        }
    });
}
void MainComponent::timerCallback()
{
    if (! indexer) return;
    auto p = indexer->progress();
    const auto s = smart ? smart->progress() : SmartProgress {};
    const auto tg = tagging ? tagging->progress() : TaggingProgress {};
    const auto at = autoTag ? autoTag->progress() : AutoTagProgress {};
    const auto sp = splitter ? splitter->progress() : SplitProgress {};
    const auto ep = extractor ? extractor->progress() : ExtractProgress {};
    const bool smartRunning = s.running;
    const bool taggingRunning = tg.running;
    const bool autoTagRunning = at.running;
    const bool splitRunning = sp.running;
    const bool extractRunning = ep.running;
    const bool busy = p.running || smartRunning || taggingRunning || autoTagRunning
        || splitRunning || extractRunning;
    pause.setEnabled(! splitRunning && (p.running || smartRunning)); cancel.setEnabled(busy);
    addFolder.setEnabled(! busy); rescan.setEnabled(! busy);
    reanalyze.setEnabled(! busy); newFiles.setEnabled(! busy);
    smartButton.setEnabled(! busy);
    pause.setButtonText((p.running ? p.paused : s.paused) ? "Resume" : "Pause");
    if (splitRunning || splitWasRunning)
    {
        if (splitRunning && splitContent != nullptr)
            splitContent->setProgress(sp, (juce::Time::getMillisecondCounter() - splitStartMs) / 1000.0);
        status.setText("Splitting " + juce::String(sp.processed) + "/" + juce::String(sp.total)
            + (sp.current.isNotEmpty() ? "  |  " + sp.current : juce::String()),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (extractRunning || extractWasRunning)
    {
        if (extractRunning && extractContent != nullptr)
            extractContent->setProgress(ep,
                (juce::Time::getMillisecondCounter() - extractStartMs) / 1000.0);
        status.setText("Extracting " + juce::String(ep.processedFiles) + "/"
            + juce::String(ep.totalFiles) + " file(s)  |  "
            + juce::String(ep.written) + " sample(s)"
            + (ep.current.isNotEmpty() ? "  |  " + ep.current : juce::String()),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (taggingRunning || taggingWasRunning)
    {
        juce::String stage;
        switch (tg.stage)
        {
            case TaggingStage::Preparing: stage = "Preparing"; break;
            case TaggingStage::Applying: stage = "Applying"; break;
            case TaggingStage::Complete: stage = "Complete"; break;
            case TaggingStage::Cancelled: stage = "Cancelled"; break;
            case TaggingStage::Failed: stage = "Failed"; break;
            case TaggingStage::Idle: break;
        }
        status.setText(tg.operation + (stage.isNotEmpty() ? "  |  " + stage : juce::String())
            + (tg.total > 0 ? "  " + juce::String(tg.processed) + "/" + juce::String(tg.total) : juce::String())
            + (tg.clearing ? "  removed " : "  applied ") + juce::String(tg.applied)
            + "  errors " + juce::String(tg.errors)
            + (tg.running && tg.current.isNotEmpty() ? "  |  " + tg.current : juce::String())
            + (tg.running ? juce::String() : "  |  " + tg.message), juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (autoTagRunning || autoTagWasRunning)
    {
        status.setText(at.message
            + (at.total > 0 ? "  " + juce::String(at.processed) + "/" + juce::String(at.total) : juce::String())
            + "  proposals " + juce::String(at.proposals) + "  applied " + juce::String(at.applied)
            + "  skipped " + juce::String(at.skipped) + "  errors " + juce::String(at.errors),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (p.running || wasRunning)
    {
        juce::String text = (p.paused ? "Paused  |  " : juce::String()) + p.message + "  "
            + juce::String(p.processed) + "/" + juce::String(p.total) + "  |  analyzed " + juce::String(p.analyzed)
            + "  skipped " + juce::String(p.skipped) + "  errors " + juce::String(p.failed)
            + "  duplicates " + juce::String(p.duplicates);
        if (p.discImages + p.discSkipped + p.discFailed + p.discEntries > 0)
            text += "  |  disc " + juce::String(p.discEntries) + " entries, "
                + juce::String(p.discImages) + " images"
                + (p.discSkipped > 0 ? ", cached " + juce::String(p.discSkipped) : juce::String())
                + (p.discFailed > 0 ? ", failed " + juce::String(p.discFailed) : juce::String());
        text += p.running ? "  |  " + p.current : juce::String();
        status.setText(text, juce::dontSendNotification);
        status.setTooltip(p.discMessage.isNotEmpty() ? p.discMessage : text);
    }
    else if (smartRunning || smartWasRunning)
    {
        juce::String stage;
        switch (s.stage)
        {
            case SmartStage::Scanning: stage = "Scanning"; break;
            case SmartStage::Verifying: stage = "Verifying"; break;
            case SmartStage::Extracting: stage = "Reading metadata"; break;
            case SmartStage::Publishing: stage = "Saving"; break;
            case SmartStage::Complete: stage = "Complete"; break;
            case SmartStage::Cancelled: stage = "Cancelled"; break;
            case SmartStage::Failed: stage = "Failed"; break;
            case SmartStage::Idle: break;
        }
        status.setText((s.paused ? "Paused  |  " : juce::String()) + stage
            + (s.message.isNotEmpty() ? "  |  " + s.message : juce::String())
            + (s.total > 0 ? "  " + juce::String(s.processed) + "/" + juce::String(s.total) : juce::String())
            + "  errors " + juce::String(s.failed) + "  stale " + juce::String(s.stale)
            + (s.running && s.current.isNotEmpty() ? "  |  " + s.current : juce::String()),juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    if (p.running)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastRefreshMs >= 1500) { lastRefreshMs = now; refreshIfCatalogChanged(); }
    }
    if (wasRunning && ! p.running)
    {
        // A cancelled scan can still have written complete rows; keep derived groups truthful.
        if (p.cancelled)
        {
            try { writeRepo->refreshDuplicates(); }
            catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
        }
        reloadLibrary(); folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored); refreshFavorites();
        // New or changed audio invalidates its smart result; rebuild metadata in the background.
        if (! p.cancelled && smart && ! smart->progress().running) smart->start();
    }
    wasRunning = p.running;
    if (smartWasRunning && ! smartRunning) reloadLibrary();
    smartWasRunning = smartRunning;
    if (taggingWasRunning && ! taggingRunning) reloadLibrary();
    taggingWasRunning = taggingRunning;
    if (autoTagPreparing && autoTagWasRunning && ! autoTagRunning)
    {
        autoTagPreparing = false;
        AutoTagResult result;
        if (autoTag != nullptr && autoTag->takeResult(result))
        {
            if (result.proposals.empty())
                status.setText(result.message + "  checked " + juce::String(result.checked)
                    + "  no evidence " + juce::String(result.noEvidence), juce::dontSendNotification);
            else
                showAutoTagDialog(std::move(result));
        }
        else
            status.setText(at.message, juce::dontSendNotification);
    }
    autoTagWasRunning = autoTagRunning;
    if (splitWasRunning && ! splitRunning) finishSplit();
    splitWasRunning = splitRunning;
    if (extractWasRunning && ! extractRunning) finishExtract();
    extractWasRunning = extractRunning;
    if (! pendingSplitScan.isEmpty() && splitScanApproved && indexer && ! indexer->progress().running
        && ! (smart && smart->progress().running)
        && ! (tagging && tagging->progress().running)
        && ! (autoTag && autoTag->progress().running)
        && ! isSplitRunning())
    {
        const auto folders = pendingSplitScan;
        pendingSplitScan.clear();
        if (indexer->startFoldersNewOnly(folders))
        {
            wasRunning = true;
            status.setText("Indexing split parts", juce::dontSendNotification);
        }
        else
        {
            pendingSplitScan = folders;
        }
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (persistSessionState && sessionDirty && now - lastSessionSaveMs >= 500) saveSessionState();
    if (pendingSearch.trim() != filter.text && now - lastSearchChange >= 250)
    {
        filter.text = pendingSearch.trim();
        pageOffset = 0;
        reloadLibrary();
    }
}
void MainComponent::lookAndFeelChanged()
{
    const bool showBrand = brandedLogo && theme::isDark;
    heading.setVisible(! showBrand); subtitle.setVisible(! showBrand);
    subtitle.setColour(juce::Label::textColourId,theme::muted);
    libraryHeading.setColour(juce::Label::textColourId,theme::muted);
    rootsLabel.setColour(juce::Label::textColourId,theme::muted);
    favoritesLabel.setColour(juce::Label::textColourId,theme::muted);
    favoritesSplitter.repaint();
    status.setColour(juce::Label::textColourId,theme::muted);
    tagsLabel.setColour(juce::Label::textColourId,theme::muted);
    stageLabel.setColour(juce::Label::textColourId,theme::muted);
    search.setTextToShowWhenEmpty("Search: *.rx2, ext:wav, type:pad, key:Am, C4",theme::muted);
    search.setJustification(juce::Justification::centredLeft);
    search.setIndents(10, 0);
    repaint();
}
void MainComponent::darkModeSettingChanged()
{
    if (appSettings.theme == ThemeMode::System) refreshTheme();
}
void MainComponent::refreshTheme()
{
    theme::apply(appSettings.theme);
    look.refreshColours();
    sendLookAndFeelChange();
    repaint();
}
bool MainComponent::contributionConsentCurrent() const
{
    return appSettings.contributionConsentVersion >= AppSettings::currentContributionConsentVersion;
}

void MainComponent::startContribution()
{
    if (contributionUploader != nullptr || writeRepo == nullptr) return;
    contribute::Uploader::Source source;
    juce::Component::SafePointer<MainComponent> safe(this);
    source.allowed = [safe]()
    {
        auto* self = safe.getComponent();
        if (self == nullptr) return false;
        const juce::ScopedLock lock(self->contributionLock);
        return self->persistSessionState && ! self->embeddedUi
            && self->appSettings.contributeTrainingData && self->contributionConsentCurrent()
            && self->appSettings.contributionInstallId.isNotEmpty();
    };
    source.installId = [safe]()
    {
        if (safe == nullptr) return juce::String();
        const juce::ScopedLock lock(safe->contributionLock);
        return safe->appSettings.contributionInstallId;
    };
    source.storeInstallId = [safe](const juce::String& id)
    {
        if (safe == nullptr) return;
        {
            const juce::ScopedLock lock(safe->contributionLock);
            safe->appSettings.contributionInstallId = id;
        }
        try { safe->settingsStore.update([&id](AppSettings& saved) { saved.contributionInstallId = id; }); }
        catch (...) {}
    };
    source.endpoint = [safe]()
    {
        if (safe == nullptr) return AppSettings::defaultContributionEndpoint();
        return safe->appSettings.contributionEndpoint.isNotEmpty()
            ? safe->appSettings.contributionEndpoint : AppSettings::defaultContributionEndpoint();
    };
    source.watermark = [safe]()
    {
        if (safe == nullptr || safe->writeRepo == nullptr) return (int64_t) 0;
        try { return safe->writeRepo->contributionWatermark(); } catch (...) { return (int64_t) 0; }
    };
    source.commitWatermark = [safe](int64_t value)
    {
        if (safe == nullptr || safe->writeRepo == nullptr) return;
        try { safe->writeRepo->setContributionWatermark(value); } catch (...) {}
    };
    source.fetch = [safe](int64_t after, int limit)
    {
        if (safe == nullptr || safe->writeRepo == nullptr) return std::vector<contribute::Record>();
        std::vector<contribute::Record> out;
        try
        {
            for (auto& row : safe->writeRepo->contributionRecords(after, limit))
            {
                contribute::Record record;
                record.recordId = row.recordId;
                record.type = row.targetType;
                record.typeConfirmed = row.typeConfirmed;
                record.tags = row.tags;
                record.featureSchema = row.featureSchema >= 2 && row.featuresV2.size() == 84 ? 2 : 1;
                record.features = record.featureSchema == 2 ? row.featuresV2 : row.features;
                record.key = row.key;
                record.bpm = row.bpm;
                record.hasBpm = row.hasBpm;
                record.updatedAt = row.updatedAt;
                out.push_back(std::move(record));
            }
        }
        catch (...) {}
        return out;
    };
    source.vocabulary = [safe]()
    {
        contribute::Vocabulary vocabulary;
        // Only names that ship with the app count as standard. A user's custom type must be
        // reported as custom, even though the repository knows it.
        vocabulary.types = standardSampleTypeNames();
        vocabulary.tags = {};
        if (safe != nullptr && safe->readRepo != nullptr)
        {
            try
            {
                vocabulary.tags = safe->readRepo->builtinTagNames();
            }
            catch (...) {}
        }
        return vocabulary;
    };
   #if JUCE_WINDOWS
    const juce::String osName = "win";
   #else
    const juce::String osName = "mac";
   #endif
    contributionUploader = std::make_shared<contribute::Uploader>(
        std::make_unique<contribute::WebTransport>(), std::move(source), version(), osName);
    contributionScheduler = std::make_unique<contribute::Scheduler>(
        *contributionUploader, [safe]()
        {
            auto* self = safe.getComponent();
            return self != nullptr && self->appSettings.contributeTrainingData
                && self->contributionConsentCurrent() && self->appSettings.contributionInstallId.isNotEmpty();
        });
    contributionScheduler->start();
}

void MainComponent::askContributionConsent()
{
    if (contributionConsentCurrent()) return;
    const auto message = juce::String(
        "Sonicator can send anonymous training data: the type you set, the tags you assign or accept "
        "from an AutoTag suggestion, tempo and key of a sample, plus 14 acoustic measurements as "
        "plain numbers. Audio, file names, folder names and paths are never sent, and nothing "
        "identifies you or your computer.\n\n"
        "Confirmations you make from now on are included. Tag assignments from older versions are "
        "not uploaded, and rows with unknown origin stay on your computer. You can review the exact "
        "data, stop at any time and delete what was sent in Settings > Library.\n\n"
        "Participate in training data collection?");
    juce::Component::SafePointer<MainComponent> safe(this);
    dialogs::showAlert(*this, "Help train the model", message,
        {{ "Not now", 0 }, { "Participate", 1 }},
        [safe](int result)
        {
            if (safe == nullptr) return;
            const bool accepted = result == 1;
            auto& settings = safe->appSettings;
            settings.contributionConsentVersion = AppSettings::currentContributionConsentVersion;
            settings.contributeTrainingData = accepted;
            if (accepted && settings.contributionInstallId.isEmpty())
                settings.contributionInstallId = contribute::Uploader::makeUuid();
            try
            {
                safe->settingsStore.update([&settings](AppSettings& saved)
                {
                    saved.contributionConsentVersion = settings.contributionConsentVersion;
                    saved.contributeTrainingData = settings.contributeTrainingData;
                    saved.contributionInstallId = settings.contributionInstallId;
                });
            }
            catch (...) {}
        });
}

void MainComponent::setContributionEnabledFromPanel(bool wanted,
                                                    juce::Component::SafePointer<SettingsPanel> panel)
{
    if (wanted && ! contributionConsentCurrent())
    {
        askContributionConsent();
        if (panel != nullptr) panel->setContributionEnabled(appSettings.contributeTrainingData);
        return;
    }
    appSettings.contributeTrainingData = wanted;
    if (wanted && appSettings.contributionInstallId.isEmpty())
        appSettings.contributionInstallId = contribute::Uploader::makeUuid();
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.contributeTrainingData = appSettings.contributeTrainingData;
            saved.contributionInstallId = appSettings.contributionInstallId;
        });
    }
    catch (...) {}
    if (panel != nullptr) panel->setContributionEnabled(appSettings.contributeTrainingData);
}

void MainComponent::deleteContributionData(std::function<void(bool, juce::String)> done)
{
    if (contributionUploader == nullptr)
    {
        if (done) done(false, "Contribution is not available.");
        return;
    }
    auto uploader = contributionUploader;
    juce::Component::SafePointer<MainComponent> safe(this);
    juce::Thread::launch([uploader, safe, done]() mutable
    {
        const auto outcome = uploader->deleteMine();
        const bool ok = outcome.sent;
        const auto error = outcome.error;
        juce::MessageManager::callAsync([safe, ok, error, done]() mutable
        {
            if (safe != nullptr && ok)
            {
                {
                    const juce::ScopedLock lock(safe->contributionLock);
                    safe->appSettings.contributeTrainingData = false;
                    safe->appSettings.contributionInstallId =
                        safe->settingsStore.load().contributionInstallId;
                }
                try { safe->settingsStore.update([](AppSettings& saved) { saved.contributeTrainingData = false; }); }
                catch (...) {}
            }
            if (done) done(ok, error);
        });
    });
}

void MainComponent::wireContributionPanel(SettingsPanel& panel)
{
    juce::Component::SafePointer<SettingsPanel> safePanel(&panel);
    panel.setContributionEnabled(appSettings.contributeTrainingData && contributionConsentCurrent());
    panel.onContributionToggle = [this, safePanel](bool wanted)
    {
        setContributionEnabledFromPanel(wanted, safePanel);
    };
    panel.onContributionPreview = [this]()
    {
        if (contributionUploader == nullptr) startContribution();
        return contributionUploader != nullptr ? contributionUploader->previewJson() : juce::String("{}");
    };
    panel.onContributionDelete = [this, safePanel](std::function<void(bool, juce::String)> done)
    {
        deleteContributionData([safePanel, done](bool ok, juce::String error)
        {
            if (safePanel != nullptr) safePanel->setContributionEnabled(false);
            if (done) done(ok, error);
        });
    };
}

void MainComponent::applySettings(const AppSettings& next)
{
    // The settings dialog edits theme, scale and language only. Other fields (catalog path,
    // filters, page size, preview toggles, window bounds) belong to their own writers and must
    // not be replaced by a stale copy taken when the dialog opened.
    appSettings.theme = next.theme;
    appSettings.scalePercent = AppSettings::clampScale(next.scalePercent);
    appSettings.language = next.language;
    refreshTheme();
    // The global scale factor belongs to the whole process: inside a DAW it would resize the
    // host's own windows. The plugin follows the host's scaling instead.
    if (! embeddedUi)
        juce::Desktop::getInstance().setGlobalScaleFactor(
            juce::jlimit(0.75f, 2.0f, appSettings.scalePercent / 100.0f));
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.theme = appSettings.theme;
            saved.scalePercent = appSettings.scalePercent;
            saved.language = appSettings.language;
        });
    }
    catch (const std::exception& e)
    {
        status.setText("Settings applied but not saved: " + juce::String::fromUTF8(e.what()),juce::dontSendNotification);
    }
}
void MainComponent::showEmbeddedSettings()
{
    if (embeddedSettingsHost != nullptr) { embeddedSettingsHost->toFront(true); return; }
    auto panel = std::make_unique<SettingsPanel>(appSettings,dbPath,indexer && indexer->progress().running);
    panel->setComponentID("settingsPanel");
    panel->onSettingsChanged = [this](AppSettings next) { applySettings(next); };
    wireContributionPanel(*panel);
    panel->setLookAndFeel(&look);
    panel->setPluginMode(true);
    panel->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->closeEmbeddedSettings();
    };
    auto overlay = std::make_unique<EmbeddedDialogOverlay>(std::move(panel));
    overlay->setComponentID("settingsOverlay");
    overlay->onDismiss = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->closeEmbeddedSettings();
    };
    addAndMakeVisible(*overlay);
    overlay->setBounds(getLocalBounds());
    embeddedSettingsPanel = dynamic_cast<SettingsPanel*>(overlay->getChildComponent(0));
    embeddedSettingsHost = overlay.get();
    overlay.release()->toFront(true);
}
void MainComponent::closeEmbeddedSettings()
{
    embeddedSettingsPanel = nullptr;
    auto* host = embeddedSettingsHost.getComponent();
    embeddedSettingsHost = nullptr;
    resized();
    if (host == nullptr) return;
    // The close button runs inside its own click callback: delete the overlay after the event
    // finishes dispatching, and only while it is still alive.
    juce::Component::SafePointer<juce::Component> safeHost(host);
    juce::MessageManager::callAsync([safeHost]() mutable
    {
        if (safeHost != nullptr) delete safeHost.getComponent();
    });
}
void MainComponent::showSettings()
{
    if (embeddedUi) { showEmbeddedSettings(); return; }
    if (settingsDialog != nullptr) { settingsDialog->toFront(true); return; }
    const bool indexing = indexer && indexer->progress().running;
    auto* panel = new SettingsPanel(appSettings,dbPath,indexing);
    panel->onSettingsChanged = [this](AppSettings next) { applySettings(next); };
    panel->onShowAudio = [this] { showAudioSettings(); };
    wireContributionPanel(*panel);
    panel->onBackupRequested = [this](juce::String& error) -> juce::File
    {
        try { return backupCatalog(); }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8(e.what());
            return {};
        }
    };
    panel->onRelocateRequested = [this]
    {
        if (settingsDialog != nullptr) settingsDialog->exitModalState(0);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->showRelocateDialog();
        });
    };
    panel->onDatabaseChosen = [this, safePanel = juce::Component::SafePointer<SettingsPanel>(panel)]
        (const juce::File& file, bool moveCurrent)
        {
            juce::String error;
            if (switchCatalog(file,moveCurrent,error))
            {
                if (safePanel != nullptr)
                {
                    safePanel->setDatabaseFile(dbPath);
                    safePanel->setDatabaseStatus(moveCurrent
                        ? "Catalog copied and switched. The previous file was kept."
                        : "Catalog opened.",false);
                }
                if (! moveCurrent && ! missingRoots().isEmpty() && settingsDialog != nullptr)
                    settingsDialog->exitModalState(0);
            }
            else if (safePanel != nullptr) safePanel->setDatabaseStatus(error,true);
        };
    panel->setLookAndFeel(&look);
    auto* window = new SettingsDialogWindow();
    window->setContentOwned(panel, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    settingsDialog = window;
    window->enterModalState(true, nullptr, true);
}
bool MainComponent::switchCatalog(const juce::File& target, bool moveCurrent, juce::String& error)
{
    error.clear();
    if (indexer && indexer->progress().running)
    {
        error = "Stop indexing before switching catalogs.";
        return false;
    }
    if (isSplitRunning())
    {
        error = "Wait for Split to finish before switching catalogs.";
        return false;
    }
    if (target.getFullPathName().isEmpty())
    {
        error = "Choose a database file.";
        return false;
    }
    if (target.getFullPathName() == dbPath.getFullPathName())
    {
        error = "This catalog is already open.";
        return false;
    }
    if (moveCurrent && target.existsAsFile())
    {
        error = "Choose a new file for the catalog copy.";
        return false;
    }
    if (! moveCurrent && ! SampleRepository::isCatalog(target))
    {
        error = target.existsAsFile() ? "That file is not a SONICATOR catalog." : "That catalog does not exist.";
        return false;
    }
    if (moveCurrent)
    {
        try { writeRepo->backupTo(target); }
        catch (const std::exception& e)
        {
            error = "Cannot copy the catalog: " + juce::String::fromUTF8(e.what());
            return false;
        }
    }
    std::unique_ptr<Database> newWriteDb, newReadDb;
    std::unique_ptr<SampleRepository> newWriteRepo, newReadRepo;
    try
    {
        newWriteDb = std::make_unique<Database>(target);
        newWriteRepo = std::make_unique<SampleRepository>(*newWriteDb);
        newWriteRepo->createTables();
        purgeUnsupportedRows(*newWriteRepo);
        newReadDb = std::make_unique<Database>(target);
        newReadRepo = std::make_unique<SampleRepository>(*newReadDb);
    }
    catch (const std::exception& e)
    {
        error = "Cannot open the catalog: " + juce::String::fromUTF8(e.what());
        if (moveCurrent) error += " The copy was kept at " + target.getFullPathName();
        return false;
    }
    if (autoTagDialog != nullptr) { autoTagDialog->exitModalState(0); autoTagDialog = nullptr; }
    closeSplitDialog();
    if (splitter) { splitter->cancel(); splitter->waitUntilFinished(-1); splitter.reset(); }
    pendingSplitScan.clear();
    splitScanApproved = false;
    splitWasRunning = false;
    if (autoTag) { autoTag->cancel(); autoTag->waitUntilFinished(-1); autoTag.reset(); }
    if (tagging) { tagging->cancel(); tagging->waitUntilFinished(-1); tagging.reset(); }
    if (smart) { smart->cancel(); smart->waitUntilFinished(-1); smart.reset(); }
    if (indexer) { indexer->cancel(); indexer->waitUntilFinished(-1); indexer.reset(); }
    if (contributionScheduler != nullptr)
    {
        contributionScheduler->stop();
        contributionScheduler.reset();
    }
    contributionUploader.reset();
    cancelFeedback = true;
    if (typeFeedbackWorker && typeFeedbackWorker->joinable()) typeFeedbackWorker->join();
    cancelFeedback = false;
    writeRepo.reset(); writeDb.reset(); readRepo.reset(); readDb.reset();
    writeDb = std::move(newWriteDb); writeRepo = std::move(newWriteRepo);
    readDb = std::move(newReadDb); readRepo = std::move(newReadRepo);
    AcousticClassifierService::getSharedAdaptiveClassifier().loadFromRepository(*readRepo);
    loadShippedGlobalPrototypes();
    indexer = std::make_unique<IndexingService>(*writeRepo);
    smart = std::make_unique<SmartAnalysisService>(*writeRepo);
    tagging = std::make_unique<TaggingService>(*writeRepo);
    tagging->onTagsChanged = [safe = juce::Component::SafePointer<MainComponent>(this)](std::vector<int64_t> ids)
    {
        juce::MessageManager::callAsync([safe, ids = std::move(ids)]
        {
            if (safe != nullptr) safe->triggerAdaptiveLearning(ids);
        });
    };
    autoTag = std::make_unique<AutoTagService>(*writeRepo);
    splitter = std::make_unique<SplitService>();
    taggingWasRunning = false;
    autoTagPreparing = false;
    autoTagWasRunning = false;
    const auto previous = dbPath;
    dbPath = target;
    const auto isDefault = dbPath.getFullPathName() == defaultDatabaseFile().getFullPathName();
    appSettings.databasePath = isDefault ? juce::String() : dbPath.getFullPathName();
    selectedFolder.clear(); selectedSampleId = 0; pageOffset = 0;
    sessionSamplePath.clear(); markSessionStateDirty();
    favorites.clearSelection();
    filter.text.clear(); filter.includeTags.clear(); filter.excludeTags.clear();
    filter.matchAnyTag = false; filter.starredOnly = false;
    filter.hideDuplicates = false; filter.hideEmpty = appSettings.hideEmpty; filter.duplicateHash.clear();
    search.clear(); pendingSearch.clear();
    analysisDetails.setText("Select a sample to inspect its measured audio features.",juce::dontSendNotification);
    selectedTags.setTags({});
    writeRepo->refreshDuplicates();
    folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored);
    refreshFavorites();
    reloadLibrary();
    juce::String message = "Catalog: " + dbPath.getFullPathName();
    if (moveCurrent) message += "  |  previous catalog kept at " + previous.getFullPathName();
    try
    {
        settingsStore.update([this](AppSettings& saved) { saved.databasePath = appSettings.databasePath; });
    }
    catch (const std::exception& e)
    {
        message += "  |  settings were not saved: " + juce::String::fromUTF8(e.what());
    }
    status.setText(message,juce::dontSendNotification);
    if (! moveCurrent && ! missingRoots().isEmpty())
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->showRelocateDialog();
        });
    }
    return true;
}
void MainComponent::showAudioSettings()
{
    if (embeddedUi) return; // the host owns the audio and MIDI devices
    if (audioDialog != nullptr) { audioDialog->toFront(true); return; }
    auto* selector = new juce::AudioDeviceSelectorComponent(engine.deviceManager(),0,0,0,2,true,false,true,false);
    selector->setLookAndFeel(&look);
    selector->setSize(460,500);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector); options.dialogTitle = "Audio/MIDI Settings";
    options.dialogBackgroundColour = theme::panel; options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false; options.resizable = false; audioDialog = options.launchAsync();
    if (audioDialog != nullptr) audioDialog->setLookAndFeel(&look);
}
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(sidebarBounds.toFloat(), 10.0f);
    g.fillRoundedRectangle(analysisBounds.toFloat(), 10.0f);
    g.setColour(theme::line); g.drawHorizontalLine(85,0,(float) getWidth());
    if (logoWordmark.isValid() && theme::isDark)
        g.drawImage(logoWordmark, juce::Rectangle<float>(16,8,310,70),
                    juce::RectanglePlacement::onlyReduceInSize | juce::RectanglePlacement::centred);
}
void MainComponent::resized()
{
    if (embeddedSettingsHost != nullptr)
        embeddedSettingsHost->setBounds(getLocalBounds());
    auto r = getLocalBounds(); headerBounds = r.removeFromTop(86);
    auto header = headerBounds.reduced(16,14);
    auto branding = header.removeFromLeft(310); header.removeFromLeft(32);
    heading.setBounds(branding.removeFromTop(30)); subtitle.setBounds(branding);
    settings.setBounds(header.removeFromRight(115).withSizeKeepingCentre(115,32));
    header.removeFromRight(16);
    search.setBounds(header.withSizeKeepingCentre(header.getWidth(),34));
    selectionStatus.setBounds(r.removeFromBottom(24).reduced(12,1));
    preview.setBounds(r.removeFromBottom(156));
    auto footer = r.removeFromBottom(42).reduced(12,5);
    cancel.setBounds(footer.removeFromRight(70)); footer.removeFromRight(6);
    pause.setBounds(footer.removeFromRight(70)); footer.removeFromRight(6);
    status.setBounds(footer);
    sidebarBounds = r.removeFromLeft(getWidth() < 1100 ? 190 : 240); analysisBounds = r.removeFromRight(getWidth() < 1100 ? 230 : 270);
    auto sidebar = sidebarBounds.reduced(14,16);
    libraryMenu.setBounds(sidebar.removeFromTop(40)); sidebar.removeFromTop(16);
    if (favorites.isVisible() && ! appSettings.favoriteFolders.isEmpty())
    {
        const int maxHeight = juce::jmax(80, sidebar.getHeight() - 24);
        favoritePanelHeight = juce::jlimit(60, maxHeight, favoritePanelHeight);
        auto area = sidebar.removeFromBottom(favoritePanelHeight);
        favoritesSplitter.setVisible(true);
        favoritesSplitter.setBounds(sidebar.removeFromBottom(10));
        favoritesLabel.setBounds(area.removeFromTop(18));
        favorites.setBounds(area);
    }
    else favoritesSplitter.setVisible(false);
    folders.setBounds(sidebar);
    auto analysis = analysisBounds.reduced(16,16);
    analysisHeading.setBounds(analysis.removeFromTop(30)); analysis.removeFromTop(8);
    editTags.setBounds(analysis.removeFromTop(30)); analysis.removeFromTop(10);
    tagsLabel.setBounds(analysis.removeFromTop(16)); analysis.removeFromTop(4);
    selectedTags.setBounds(analysis.removeFromTop(selectedTags.getPreferredHeight(analysis.getWidth(), 2)));
    analysis.removeFromTop(8);
    const int metadataHeight = getHeight() < 700 ? 58 : 90;
    auto& sources = preview.containerSourceList();
    if (sources.isVisible())
    {
        const int sourceHeight = juce::jmin(220, juce::jmax(70, (analysis.getHeight() - metadataHeight) / 2));
        sources.setBounds(analysis.removeFromBottom(sourceHeight));
        analysis.removeFromBottom(10);
    }
    stageLabel.setBounds(analysis.removeFromBottom(metadataHeight));
    analysisViewport.setBounds(analysis);
    analysisDetails.setBounds(0,0,juce::jmax(1,analysis.getWidth()-14),550);
    auto list = r.reduced(14,14); auto bar = list.removeFromTop(32);
    pageSizeBox.setBounds(bar.removeFromRight(92)); bar.removeFromRight(8);
    nextPage.setBounds(bar.removeFromRight(30)); bar.removeFromRight(4);
    previousPage.setBounds(bar.removeFromRight(30)); countLabel.setBounds(bar);
    list.removeFromTop(4);
    auto filterBar = list.removeFromTop(30);
    tagFilter.setBounds(filterBar.removeFromLeft(64)); filterBar.removeFromLeft(6);
    starredButton.setBounds(filterBar.removeFromLeft(76)); filterBar.removeFromLeft(6);
    hideEmpty.setBounds(filterBar.removeFromLeft(186)); filterBar.removeFromLeft(6);
    clearFilters.setBounds(filterBar.removeFromLeft(92));
    list.removeFromTop(8); library.setBounds(list);
    if (discBrowser != nullptr) discBrowser->setBounds(library.getBounds());
}
} // namespace sonicator
