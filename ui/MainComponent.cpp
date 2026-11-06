#include "MainComponent.h"
#include "BinaryData.h"
#include "ui/settings/SettingsPanel.h"
#include <algorithm>
#include <iterator>
namespace sonicator {
namespace {
struct PaletteColour { const char* name; uint32_t colour; };
constexpr PaletteColour tagPalette[] = {
    { "Red", 0xffe05a5a }, { "Orange", 0xffe08a45 }, { "Yellow", 0xffe0b84a },
    { "Lime", 0xff9ad06a }, { "Green", 0xff6ad0a0 }, { "Teal", 0xff4fb8c8 },
    { "Blue", 0xff4f8fd0 }, { "Violet", 0xff7a6aea }, { "Purple", 0xffa96ae0 },
    { "Pink", 0xffd06ab8 }, { "Rose", 0xffd06a7a }, { "Grey", 0xff8a8f9c }
};
constexpr int pageSize = 10000;
}
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
    setLookAndFeel(&look); look.refreshColours(); setWantsKeyboardFocus(true);
    logoWordmark = juce::ImageFileFormat::loadFrom(BinaryData::logosilver_png, BinaryData::logosilver_pngSize);
    for (auto* c : std::array<juce::Component*,28>{&preview,&library,&search,&addFolder,&rescan,&reanalyze,&newFiles,
         &pause,&cancel,&settings,&backup,&tagFilter,&clearFilters,&editTags,&previousPage,&nextPage,&heading,&subtitle,
         &libraryHeading,&rootsLabel,&status,&countLabel,&analysisHeading,&analysisDetails,&tagsLabel,
         &selectedTags,&analysisViewport,&stageLabel})
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
    stageLabel.setText("CORE BROWSER  /  PHASE 1\n\nClassification, BPM and key\nare not available in this stage.",juce::dontSendNotification);
    stageLabel.setFont(juce::FontOptions(11)); stageLabel.setColour(juce::Label::textColourId,theme::muted);
    stageLabel.setJustificationType(juce::Justification::topLeft);
    status.setFont(juce::FontOptions(12)); status.setColour(juce::Label::textColourId,theme::muted);
    countLabel.setFont(juce::FontOptions(12));
    tagsLabel.setText("TAGS",juce::dontSendNotification);
    tagsLabel.setFont(juce::FontOptions(11)); tagsLabel.setColour(juce::Label::textColourId,theme::muted);
    search.setTextToShowWhenEmpty("Search entire library...",theme::muted);
    search.onTextChange = [this]
    {
        pendingSearch = search.getText();
        lastSearchChange = juce::Time::getMillisecondCounter();
    };
    search.onReturnKey = [this]
    {
        pendingSearch = search.getText();
        filter.text = pendingSearch.trim(); pageOffset = 0; reloadLibrary();
    };
    search.onEscapeKey = [this]
    {
        search.clear(); pendingSearch.clear(); filter.text.clear(); pageOffset = 0; reloadLibrary();
    };
    search.setComponentID("search");
    selectedTags.setComponentID("selectedTags");
    library.setComponentID("library"); status.setComponentID("importStatus"); analysisDetails.setComponentID("analysisDetails");
    library.onSelected = [this](const SampleRecord& r) { selectSample(r); };
    library.onSortChanged = [this](int column, bool ascending)
    {
        switch (column)
        {
            case 2: filter.sort = LibrarySort::Format; break;
            case 3: filter.sort = LibrarySort::Duration; break;
            case 4: filter.sort = LibrarySort::Rate; break;
            case 5: filter.sort = LibrarySort::Channels; break;
            case 6: filter.sort = LibrarySort::Status; break;
            default: filter.sort = LibrarySort::Name; break;
        }
        filter.ascending = ascending; pageOffset = 0; reloadLibrary();
    };
    addAndMakeVisible(folders); rootsLabel.setVisible(false);
    folders.onFolder = [this](juce::String path) { selectedFolder = std::move(path); pageOffset = 0; reloadLibrary(); };
    addFolder.onClick = [this] { chooseFolder(); };
    rescan.onClick = [this] { requestScan(false); };
    newFiles.onClick = [this] { scanRoots(false, true); };
    reanalyze.onClick = [this] { requestScan(true); };
    backup.onClick = [this]
    {
        try { backupCatalog(); }
        catch (const std::exception& e) { status.setText("Backup failed: " + juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
    };
    backup.setTooltip("Save a consistent copy of the catalog database");
    pause.onClick = [this] { if (indexer) indexer->setPaused(! indexer->progress().paused); };
    cancel.onClick = [this] { if (indexer) indexer->cancel(); };
    settings.onClick = [this] { showSettings(); };
    tagFilter.onClick = [this] { showTagFilterMenu(); };
    clearFilters.onClick = [this]
    {
        search.clear(); pendingSearch.clear();
        filter.text.clear(); filter.includeTags.clear(); filter.excludeTags.clear(); filter.matchAnyTag = false;
        pageOffset = 0; reloadLibrary();
    };
    editTags.onClick = [this] { showEditTagsMenu(); };
    previousPage.onClick = [this] { pageOffset = juce::jmax(0,pageOffset-pageSize); reloadLibrary(); };
    nextPage.onClick = [this] { pageOffset += pageSize; reloadLibrary(); };
    preview.onAction = [this] { library.focusForNavigation(); };
    for (auto* button : std::array<juce::Button*,13>{&addFolder,&rescan,&reanalyze,&newFiles,&pause,&cancel,&settings,&backup,
          &tagFilter,&clearFilters,&editTags,&previousPage,&nextPage})
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
        readDb = std::make_unique<Database>(dbPath); readRepo = std::make_unique<SampleRepository>(*readDb);
        indexer = std::make_unique<IndexingService>(*writeRepo);
        folders.refresh(*readRepo);
        reloadLibrary();
        status.setText(catalogCount > 0
            ? juce::String(catalogCount) + " samples loaded"
            : "Ready. Add a folder to begin.",juce::dontSendNotification);
    }
    catch (const std::exception& e) { status.setText("Database error: " + juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
    if (engine.error().isNotEmpty()) status.setText("Audio device: " + engine.error(),juce::dontSendNotification);
    pause.setEnabled(false); cancel.setEnabled(false);
    addFolder.setEnabled(indexer != nullptr); rescan.setEnabled(indexer != nullptr);
    reanalyze.setEnabled(indexer != nullptr); newFiles.setEnabled(indexer != nullptr);
    backup.setEnabled(indexer != nullptr);
    sendLookAndFeelChange();
    startTimerHz(10);
    juce::Desktop::getInstance().addDarkModeSettingListener(this);
}
MainComponent::~MainComponent()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener(this);
    stopTimer(); chooser.reset(); settingsDialog.deleteAndZero(); audioDialog.deleteAndZero();
    indexer.reset(); setLookAndFeel(nullptr);
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
        writeRepo->addRoot(folder.getFullPathName()); folders.refresh(*readRepo);
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
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Rescan library")
            .withMessage("Walk every registered folder and analyze files that are new, changed or previously failed. Unchanged analyzed files are skipped.\n\nA large library can take a long time.")
            .withButton("Rescan", 1).withButton("Cancel", 0),
            [safe](int choice) { if (safe != nullptr && choice == 1) safe->scanRoots(false); });
        return;
    }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Reanalyze all")
        .withMessage("Recompute the measured features of every file in the catalog, including files that have not changed.\n\nOn a large library this can take a long time. Completed results are saved if you cancel.")
        .withButton("Reanalyze", 1).withButton("Cancel", 0),
        [safe](int choice) { if (safe != nullptr && choice == 1) safe->scanRoots(true); });
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
bool MainComponent::refreshIfCatalogChanged()
{
    if (! readRepo) return false;
    try { if (readRepo->count() == totalCatalogCount) return false; }
    catch (const std::exception&) { return false; }
    reloadLibrary();
    return true;
}
void MainComponent::reloadLibrary()
{
    if (! readRepo) return;
    try
    {
        totalCatalogCount = readRepo->count();
        filter.folder = selectedFolder;
        auto page = readRepo->query(filter, pageSize, pageOffset);
        if (pageOffset > 0 && pageOffset >= page.total)
        {
            pageOffset = page.total > 0 ? ((page.total - 1) / pageSize) * pageSize : 0;
            page = readRepo->query(filter, pageSize, pageOffset);
        }
        catalogCount = page.total;
        std::vector<int64_t> ids;
        ids.reserve(page.samples.size());
        for (const auto& sample : page.samples) ids.push_back(sample.id);
        const int shown = (int) page.samples.size();
        library.setSamples(std::move(page.samples));
        library.setRowTags(readRepo->tagsForSamples(ids));
        allTags = readRepo->tags();
        updateTagControls();
        juce::String text = juce::String(catalogCount) + (filter.hasTextOrTagFilter() ? " matching" : " samples")
            + "  |  " + juce::String(shown) + " on page " + juce::String(pageOffset/pageSize+1);
        if (filter.matchAnyTag && ! filter.includeTags.empty()) text += "  |  any tag";
        countLabel.setText(text,juce::dontSendNotification);
        rootsLabel.setText(readRepo->roots().isEmpty() ? "No sample folders yet.\n\nOriginal files stay untouched."
            : "FOLDERS\n\n" + readRepo->roots().joinIntoString("\n\n"),juce::dontSendNotification);
        previousPage.setEnabled(pageOffset > 0); nextPage.setEnabled(pageOffset+pageSize < catalogCount);
        updateSelectedTags();
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
void MainComponent::updateTagControls()
{
    const auto active = (int) (filter.includeTags.size() + filter.excludeTags.size());
    tagFilter.setButtonText(active > 0 ? "Tags (" + juce::String(active) + ")" : "Tags");
    tagFilter.setToggleState(active > 0 || filter.matchAnyTag, juce::dontSendNotification);
    clearFilters.setEnabled(filter.hasTextOrTagFilter());
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
    analysisDetails.setText(details,juce::dontSendNotification);
    analysisDetails.setTooltip(r.filePath);
    updateSelectedTags();
    if (! preview.loadFile(juce::File(r.filePath))) status.setText(engine.error(),juce::dontSendNotification);
}
void MainComponent::showTagFilterMenu()
{
    if (! readRepo) return;
    try { allTags = readRepo->tags(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    juce::PopupMenu menu, excludes, manage;
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
                    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Delete tag")
                        .withMessage("Remove \"" + tag.name + "\" from every sample?")
                        .withButton("Delete", 1).withButton("Cancel", 0),
                        [safe, id = tag.id](int choice)
                        {
                            if (safe == nullptr || choice != 1) return;
                            try { safe->writeRepo->deleteTag(id); safe->reloadLibrary(); }
                            catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                        });
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
    for (int i = 0; i < (int) std::size(tagPalette); ++i) menu.addItem(i+1, tagPalette[i].name);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tagFilter),
        [safe = juce::Component::SafePointer<MainComponent>(this), tagId](int result)
        {
            if (safe == nullptr || result <= 0 || result > (int) std::size(tagPalette) || safe->writeRepo == nullptr) return;
            safe->writeRepo->setTagColour(tagId, tagPalette[result-1].colour);
            safe->reloadLibrary();
        });
}
void MainComponent::showEditTagsMenu()
{
    if (! readRepo || selectedSampleId <= 0)
        return;
    const auto sampleId = selectedSampleId;
    auto assigned = readRepo->tagsForSample(sampleId);
    juce::PopupMenu menu;
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
                        safe->writeRepo->setSampleTag(sampleId, id, true);
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
            safe->writeRepo->setSampleTag(sampleId, tag.id, ! isAssigned);
            safe->reloadLibrary();
        });
}
void MainComponent::promptTagName(const juce::String& title, const juce::String& initial,
                                  std::function<void(juce::String)> onAccept)
{
    auto* window = new juce::AlertWindow(title, "Tag name:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", initial);
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safeWindow(window);
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeWindow, callback = std::move(onAccept)](int result) mutable
        {
            if (safeWindow == nullptr || result == 0 || callback == nullptr) return;
            callback(safeWindow->getTextEditorContents("name"));
        }), true);
}
void MainComponent::timerCallback()
{
    if (! indexer) return;
    auto p = indexer->progress();
    pause.setEnabled(p.running); cancel.setEnabled(p.running);
    addFolder.setEnabled(! p.running); rescan.setEnabled(! p.running);
    reanalyze.setEnabled(! p.running); newFiles.setEnabled(! p.running);
    pause.setButtonText(p.paused ? "Resume" : "Pause");
    if (p.running || wasRunning)
    {
        status.setText((p.paused ? "Paused  |  " : juce::String()) + p.message + "  "
            + juce::String(p.processed) + "/" + juce::String(p.total) + "  |  analyzed " + juce::String(p.analyzed)
            + "  skipped " + juce::String(p.skipped) + "  errors " + juce::String(p.failed)
            + "  duplicates " + juce::String(p.duplicates) + (p.running ? "  |  " + p.current : ""),juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    if (p.running)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastRefreshMs >= 1500) { lastRefreshMs = now; refreshIfCatalogChanged(); }
    }
    if (wasRunning && ! p.running) { reloadLibrary(); folders.refresh(*readRepo); }
    wasRunning = p.running;
    const auto now = juce::Time::getMillisecondCounter();
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
    status.setColour(juce::Label::textColourId,theme::muted);
    tagsLabel.setColour(juce::Label::textColourId,theme::muted);
    stageLabel.setColour(juce::Label::textColourId,theme::muted);
    search.setTextToShowWhenEmpty("Search entire library...",theme::muted);
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
    const auto database = appSettings.databasePath;
    appSettings = next;
    appSettings.databasePath = database;
    appSettings.scalePercent = AppSettings::clampScale(appSettings.scalePercent);
    refreshTheme();
    juce::Desktop::getInstance().setGlobalScaleFactor(
        juce::jlimit(0.75f, 2.0f, appSettings.scalePercent / 100.0f));
    try { settingsStore.save(appSettings); }
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
            }
            else if (safePanel != nullptr) safePanel->setDatabaseStatus(error,true);
        };
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel);
    panel->setLookAndFeel(&look);
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = theme::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    settingsDialog = options.launchAsync();
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
    if (indexer) { indexer->cancel(); indexer->waitUntilFinished(-1); indexer.reset(); }
    writeRepo.reset(); writeDb.reset(); readRepo.reset(); readDb.reset();
    writeDb = std::move(newWriteDb); writeRepo = std::move(newWriteRepo);
    readDb = std::move(newReadDb); readRepo = std::move(newReadRepo);
    indexer = std::make_unique<IndexingService>(*writeRepo);
    const auto previous = dbPath;
    dbPath = target;
    const auto isDefault = dbPath.getFullPathName() == defaultDatabaseFile().getFullPathName();
    appSettings.databasePath = isDefault ? juce::String() : dbPath.getFullPathName();
    selectedFolder.clear(); selectedSampleId = 0; pageOffset = 0;
    filter.text.clear(); filter.includeTags.clear(); filter.excludeTags.clear(); filter.matchAnyTag = false;
    search.clear(); pendingSearch.clear();
    analysisDetails.setText("Select a sample to inspect its measured audio features.",juce::dontSendNotification);
    selectedTags.setTags({});
    folders.refresh(*readRepo);
    reloadLibrary();
    juce::String message = "Catalog: " + dbPath.getFullPathName();
    if (moveCurrent) message += "  |  previous catalog kept at " + previous.getFullPathName();
    try { settingsStore.save(appSettings); }
    catch (const std::exception& e)
    {
        message += "  |  settings were not saved: " + juce::String::fromUTF8(e.what());
    }
    status.setText(message,juce::dontSendNotification);
    return true;
}
void MainComponent::showAudioSettings()
{
    if (audioDialog != nullptr) { audioDialog->toFront(true); return; }
    auto* selector = new juce::AudioDeviceSelectorComponent(engine.deviceManager(),0,0,0,2,true,false,true,false);
    selector->setLookAndFeel(&look);
    selector->setSize(460,500);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector); options.dialogTitle = "Audio output";
    options.dialogBackgroundColour = theme::panel; options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true; options.resizable = false; audioDialog = options.launchAsync();
}
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::panel); g.fillRect(sidebarBounds); g.fillRect(analysisBounds);
    g.setColour(theme::line); g.drawHorizontalLine(85,0,(float) getWidth());
    g.drawVerticalLine(sidebarBounds.getRight(),86,(float) sidebarBounds.getBottom());
    g.drawVerticalLine(analysisBounds.getX(),86,(float) analysisBounds.getBottom());
    if (logoWordmark.isValid() && theme::isDark)
        g.drawImage(logoWordmark, juce::Rectangle<float>(16,8,310,70),
                    juce::RectanglePlacement::onlyReduceInSize | juce::RectanglePlacement::centred);
}
void MainComponent::resized()
{
    auto r = getLocalBounds(); headerBounds = r.removeFromTop(86);
    auto header = headerBounds.reduced(16,14);
    auto branding = header.removeFromLeft(310); header.removeFromLeft(16);
    heading.setBounds(branding.removeFromTop(30)); subtitle.setBounds(branding);
    settings.setBounds(header.removeFromRight(115).withSizeKeepingCentre(115,32));
    header.removeFromRight(16); search.setBounds(header.withSizeKeepingCentre(header.getWidth(),34));
    preview.setBounds(r.removeFromBottom(156));
    auto footer = r.removeFromBottom(42).reduced(12,5);
    cancel.setBounds(footer.removeFromRight(70)); footer.removeFromRight(6);
    pause.setBounds(footer.removeFromRight(70)); footer.removeFromRight(6);
    backup.setBounds(footer.removeFromRight(70)); status.setBounds(footer);
    sidebarBounds = r.removeFromLeft(getWidth() < 1100 ? 190 : 240); analysisBounds = r.removeFromRight(getWidth() < 1100 ? 230 : 270);
    auto sidebar = sidebarBounds.reduced(14,16);
    libraryHeading.setBounds(sidebar.removeFromTop(30)); sidebar.removeFromTop(12);
    addFolder.setBounds(sidebar.removeFromTop(36)); sidebar.removeFromTop(10);
    auto scanRow = sidebar.removeFromTop(32);
    rescan.setBounds(scanRow.removeFromLeft((scanRow.getWidth() - 6) / 2)); scanRow.removeFromLeft(6);
    newFiles.setBounds(scanRow); sidebar.removeFromTop(8);
    reanalyze.setBounds(sidebar.removeFromTop(32)); sidebar.removeFromTop(24);
    folders.setBounds(sidebar);
    auto analysis = analysisBounds.reduced(16,16);
    analysisHeading.setBounds(analysis.removeFromTop(30)); analysis.removeFromTop(8);
    editTags.setBounds(analysis.removeFromTop(30)); analysis.removeFromTop(10);
    tagsLabel.setBounds(analysis.removeFromTop(16)); analysis.removeFromTop(4);
    selectedTags.setBounds(analysis.removeFromTop(selectedTags.getPreferredHeight(analysis.getWidth(), 2)));
    analysis.removeFromTop(8);
    stageLabel.setBounds(analysis.removeFromBottom(getHeight() < 700 ? 58 : 90));
    analysisViewport.setBounds(analysis);
    analysisDetails.setBounds(0,0,juce::jmax(1,analysis.getWidth()-14),550);
    auto list = r.reduced(14,14); auto bar = list.removeFromTop(32);
    nextPage.setBounds(bar.removeFromRight(30)); bar.removeFromRight(4);
    previousPage.setBounds(bar.removeFromRight(30)); countLabel.setBounds(bar);
    list.removeFromTop(4);
    auto filterBar = list.removeFromTop(30);
    tagFilter.setBounds(filterBar.removeFromLeft(90)); filterBar.removeFromLeft(8);
    clearFilters.setBounds(filterBar.removeFromLeft(110));
    list.removeFromTop(8); library.setBounds(list);
}
} // namespace sonicator
