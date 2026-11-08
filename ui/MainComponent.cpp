#include "MainComponent.h"
#include "ui/tagging/AutoTagDialog.h"
#include "BinaryData.h"
#include "core/database/PathRelocation.h"
#include "ui/settings/RelocateFoldersPanel.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/shell/ThemedDialogs.h"
#include "ui/platform/FileClipboard.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
namespace sonicator {
namespace {
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
MainComponent::MainComponent(const juce::File& databaseFile,bool openDevice,AppSettings initialSettings,juce::File settingsFile)
    : dbPath(databaseFile), appSettings(std::move(initialSettings)),
      settingsStore(settingsFile != juce::File() ? std::move(settingsFile) : AppSettings::defaultSettingsFile()),
      engine(openDevice), preview(engine,dbPath.getParentDirectory().getChildFile("cache"))
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
    addAndMakeVisible(favoritesLabel); addAndMakeVisible(favorites); addAndMakeVisible(favoritesSplitter);
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
        if (indexer && indexer->progress().running)
        {
            status.setText("Wait for the library scan to finish before smart analysis.",juce::dontSendNotification);
            return;
        }
        if (smart->progress().running) return;
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
        menu.addItem(1, "Add folder...", addFolder.isEnabled());
        menu.addItem(2, "Rescan", rescan.isEnabled());
        menu.addItem(3, "Rescan and analyze new files", newFiles.isEnabled());
        menu.addSeparator();
        menu.addItem(4, "Reanalyze all", reanalyze.isEnabled());
        menu.addItem(5, "Smart metadata", smartButton.isEnabled());
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
        if (indexer && indexer->progress().running) indexer->cancel();
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
        writeRepo->refreshDuplicates();
        readDb = std::make_unique<Database>(dbPath); readRepo = std::make_unique<SampleRepository>(*readDb);
        indexer = std::make_unique<IndexingService>(*writeRepo);
        smart = std::make_unique<SmartAnalysisService>(*writeRepo);
        tagging = std::make_unique<TaggingService>(*writeRepo);
        autoTag = std::make_unique<AutoTagService>(*writeRepo);
        restoringSession = true;
        if (! appSettings.lastFolder.isEmpty()
            && juce::File(appSettings.lastFolder).isDirectory()
            && readRepo->countInFolder(appSettings.lastFolder) > 0)
            selectedFolder = appSettings.lastFolder;
        folders.refresh(*readRepo);
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
                    if (isDiscImageFormat(record->format))
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
    juce::Desktop::getInstance().addDarkModeSettingListener(this);
}
MainComponent::~MainComponent()
{
    juce::PopupMenu::dismissAllActiveMenus();
    if (persistSessionState) saveSessionState();
    removeKeyListener(&enterKeyListener);
    juce::Desktop::getInstance().removeDarkModeSettingListener(this);
    stopTimer(); chooser.reset(); relocateDialog.deleteAndZero();
    settingsDialog.deleteAndZero(); audioDialog.deleteAndZero();
    smart.reset(); indexer.reset(); tagging.reset(); autoTag.reset(); setLookAndFeel(nullptr);
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
    if (! indexer || indexer->progress().running) return;
    try
    {
        writeRepo->addRoot(folder.getFullPathName()); folders.refresh(*readRepo); refreshFavorites();
        if (indexer->start({folder.getFullPathName()})) { wasRunning = true; reloadLibrary(); }
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
void MainComponent::requestScan(bool reanalyzeAll)
{
    if (! indexer || indexer->progress().running || ! readRepo) return;
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
    if (! indexer || indexer->progress().running) return;
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
            try { folders.refresh(*readRepo); }
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
        totalCatalogCount = readRepo->count();
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
            if (column == 10)
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
        text += "   |   Type: " + field(10) + "   BPM: " + field(11) + "   Key: " + field(12);
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
void MainComponent::selectSample(const SampleRecord& r)
{
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

bool MainComponent::EnterKeyListener::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return key.getKeyCode() == juce::KeyPress::returnKey && owner.openSelectedDiscIfAny();
}

bool MainComponent::openSelectedDiscIfAny()
{
    if (discBrowser != nullptr && discBrowser->isVisible()) return false;
    const auto selected = library.getSelectedSamples();
    if (selected.empty() || ! isDiscImageFormat(selected[0].format)) return false;
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

void MainComponent::openDiscImage(const SampleRecord& r)
{
    if (! isDiscImageFormat(r.format)) return;
    if (isIncompatible(r.compatibility))
    {
        status.setText(r.error.isNotEmpty() ? r.error : "This image is not supported", juce::dontSendNotification);
        return;
    }
    if (! discBrowser)
    {
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
        discBrowser->onExit = [this]
        {
            if (discBrowser != nullptr) { discBrowser->closeImage(); discBrowser->setVisible(false); }
            library.focusForNavigation();
        };
        discBrowser->onSampleSelected = [this](const juce::File& container, const juce::String& stem,
                                               SampleAudioPtr audio)
        {
            if (! discBrowser || ! discBrowser->isVisible() || discBrowser->openedImage() != container) return;
            if (! preview.loadDiscSample(container, stem, std::move(audio), false, 0, 0, false, true))
                status.setText(engine.error(), juce::dontSendNotification);
            else
                status.setText("Browsing " + container.getFileName(), juce::dontSendNotification);
        };
        addChildComponent(*discBrowser);
    }
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tagFilter),
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
    const int added = writeRepo->assignTag(sampleIds, tagId);
    if (added > 0) writeRepo->recordRecentTag(tagId);
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&editTags),
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
            if (isAssigned) safe->writeRepo->setSampleTag(sampleId, tag.id, false);
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
    try { favorites.refresh(*readRepo, appSettings.favoriteFolders, false); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
    const bool hasFavorites = ! appSettings.favoriteFolders.isEmpty();
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&favorites).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), path](int result)
        {
            if (safe != nullptr && result == 7201) safe->toggleFavoriteFolder(path);
        });
}
void MainComponent::resetTagsSelected(const std::vector<int64_t>& sampleIds)
{
    if (readRepo == nullptr || sampleIds.empty()) return;
    if (autoTagDialog != nullptr)
    {
        status.setText("Close the AutoTag review before resetting tags.",juce::dontSendNotification);
        return;
    }
    int tagged = 0;
    try { tagged = readRepo->countTaggedSamples(sampleIds); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (tagged == 0)
    {
        status.setText("The selected files have no tags to reset.",juce::dontSendNotification);
        return;
    }
    dialogs::showAlert(*this, "Reset tags",
        "Remove all tag assignments from " + juce::String(sampleIds.size())
            + " selected file(s)?\n\n" + juce::String(tagged)
            + " file(s) currently have tags.\n\nTag definitions, stars, smart overrides and original files are kept.",
        { { "Reset", 1 }, { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<MainComponent>(this), sampleIds](int choice)
        {
            if (safe == nullptr || choice != 1 || safe->tagging == nullptr) return;
            if (safe->autoTag != nullptr && safe->autoTag->progress().running)
            {
                safe->status.setText("Wait for AutoTag to finish before resetting tags.",juce::dontSendNotification);
                return;
            }
            if (safe->tagging->startResetSelected(sampleIds))
                safe->status.setText("Removing tags from selected files",juce::dontSendNotification);
            else
                safe->status.setText("Tag reset could not start.",juce::dontSendNotification);
        }, juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::resetTagsFolder(const juce::String& path, int catalogued)
{
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
    if (autoTag == nullptr || sampleIds.empty()) return;
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
    if (autoTag->startSelected(sampleIds))
    {
        autoTagPreparing = true;
        autoTagWasRunning = true;
        status.setText("Preparing AutoTag proposals for " + juce::String(sampleIds.size())
            + " selected file(s)", juce::dontSendNotification);
    }
    else
        status.setText("AutoTag could not start.", juce::dontSendNotification);
}
void MainComponent::startAutoTagFolder(const juce::String& folder)
{
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
    menu.addSeparator();
    menu.addItem(7002, "Scan folder", indexer != nullptr && ! indexer->progress().running);
    menu.addItem(7003, "Analyze smart metadata (" + juce::String(catalogued) + ")",
                 smart != nullptr && ! smart->progress().running
                     && (indexer == nullptr || ! indexer->progress().running));
    const bool autoTagIdle = autoTag != nullptr && ! autoTag->progress().running
        && (indexer == nullptr || ! indexer->progress().running)
        && (smart == nullptr || ! smart->progress().running)
        && (tagging == nullptr || ! tagging->progress().running);
    menu.addItem(7004, "AutoTag folder...", autoTagIdle && catalogued > 0);
    menu.addSeparator();
    menu.addSubMenu("Tag folder...", tagMenu, ! allTags.empty() && catalogued > 0);
    menu.addItem(7005, "Reset tags...", catalogued > 0 && autoTagIdle && autoTagDialog == nullptr);
    const juce::String folderPath = path;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&folders).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), folderPath, catalogued](int result)
        {
            if (safe == nullptr || result == 0) return;
            try
            {
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
    try { allTags = readRepo->tags(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    std::vector<int64_t> ids;
    bool allStarred = true, anyExisting = false;
    for (const auto& record : selection)
    {
        ids.push_back(record.id);
        allStarred = allStarred && record.starred;
        anyExisting = anyExisting || juce::File(record.filePath).existsAsFile();
    }
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
    for (int i = 0; i < (int) allTags.size(); ++i)
        tagsMenu.addItem(10000 + i, allTags[(size_t) i].name, true,
                         allHave[allTags[(size_t) i].id], tagDotImage(allTags[(size_t) i].colour));
    if (! allTags.empty()) tagsMenu.addSeparator();
    tagsMenu.addItem(9003, "New tag...");
    menu.addSubMenu("Tag", tagsMenu);
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
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), ids, allStarred, allHave, recent,
         files = selection, leadPath = lead.filePath, leadHash = lead.fileHash](int result)
        {
            if (safe == nullptr || result == 0) return;
            try
            {
                if (result >= 20000)
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
                else if (result == 9001)
                {
                    safe->writeRepo->setStarred(ids, ! allStarred);
                    safe->writeRepo->refreshDuplicates();
                    safe->reloadLibrary();
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
                    else safe->writeRepo->setSampleTag(ids, tag.id, false);
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
void MainComponent::timerCallback()
{
    if (! indexer) return;
    auto p = indexer->progress();
    const auto s = smart ? smart->progress() : SmartProgress {};
    const auto tg = tagging ? tagging->progress() : TaggingProgress {};
    const auto at = autoTag ? autoTag->progress() : AutoTagProgress {};
    const bool smartRunning = s.running;
    const bool taggingRunning = tg.running;
    const bool autoTagRunning = at.running;
    const bool busy = p.running || smartRunning || taggingRunning || autoTagRunning;
    pause.setEnabled(p.running || smartRunning); cancel.setEnabled(busy);
    addFolder.setEnabled(! busy); rescan.setEnabled(! busy);
    reanalyze.setEnabled(! busy); newFiles.setEnabled(! busy);
    smartButton.setEnabled(! busy);
    pause.setButtonText((p.running ? p.paused : s.paused) ? "Resume" : "Pause");
    if (taggingRunning || taggingWasRunning)
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
        status.setText((p.paused ? "Paused  |  " : juce::String()) + p.message + "  "
            + juce::String(p.processed) + "/" + juce::String(p.total) + "  |  analyzed " + juce::String(p.analyzed)
            + "  skipped " + juce::String(p.skipped) + "  errors " + juce::String(p.failed)
            + "  duplicates " + juce::String(p.duplicates) + (p.running ? "  |  " + p.current : ""),juce::dontSendNotification);
        status.setTooltip(status.getText());
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
        reloadLibrary(); folders.refresh(*readRepo); refreshFavorites();
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
void MainComponent::applySettings(const AppSettings& next)
{
    // The settings dialog edits theme, scale and language only. Other fields (catalog path,
    // filters, page size, preview toggles, window bounds) belong to their own writers and must
    // not be replaced by a stale copy taken when the dialog opened.
    appSettings.theme = next.theme;
    appSettings.scalePercent = AppSettings::clampScale(next.scalePercent);
    appSettings.language = next.language;
    refreshTheme();
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
void MainComponent::showSettings()
{
    if (settingsDialog != nullptr) { settingsDialog->toFront(true); return; }
    const bool indexing = indexer && indexer->progress().running;
    auto* panel = new SettingsPanel(appSettings,dbPath,indexing);
    panel->onSettingsChanged = [this](AppSettings next) { applySettings(next); };
    panel->onShowAudio = [this] { showAudioSettings(); };
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
    if (autoTag) { autoTag->cancel(); autoTag->waitUntilFinished(-1); autoTag.reset(); }
    if (tagging) { tagging->cancel(); tagging->waitUntilFinished(-1); tagging.reset(); }
    if (smart) { smart->cancel(); smart->waitUntilFinished(-1); smart.reset(); }
    if (indexer) { indexer->cancel(); indexer->waitUntilFinished(-1); indexer.reset(); }
    writeRepo.reset(); writeDb.reset(); readRepo.reset(); readDb.reset();
    writeDb = std::move(newWriteDb); writeRepo = std::move(newWriteRepo);
    readDb = std::move(newReadDb); readRepo = std::move(newReadRepo);
    indexer = std::make_unique<IndexingService>(*writeRepo);
    smart = std::make_unique<SmartAnalysisService>(*writeRepo);
    tagging = std::make_unique<TaggingService>(*writeRepo);
    autoTag = std::make_unique<AutoTagService>(*writeRepo);
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
    folders.refresh(*readRepo);
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
