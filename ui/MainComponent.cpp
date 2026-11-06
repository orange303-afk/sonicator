#include "MainComponent.h"
#include "BinaryData.h"
namespace sonicator {
juce::File MainComponent::defaultDatabaseFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/sonicator.sqlite");
}
MainComponent::MainComponent(const juce::File& databaseFile,bool openDevice)
    : dbPath(databaseFile), engine(openDevice), preview(engine,dbPath.getParentDirectory().getChildFile("cache"))
{
    setLookAndFeel(&look); setWantsKeyboardFocus(true);
    headerIcon = juce::ImageFileFormat::loadFrom(BinaryData::iconheader_png, BinaryData::iconheader_pngSize);
    logoWordmark = juce::ImageFileFormat::loadFrom(BinaryData::logosilver_png, BinaryData::logosilver_pngSize);
    for (auto* c : std::array<juce::Component*,20>{&preview,&library,&search,&addFolder,&rescan,&reanalyze,
         &pause,&cancel,&settings,&previousPage,&nextPage,&heading,&subtitle,&libraryHeading,&rootsLabel,
         &status,&countLabel,&analysisHeading,&analysisViewport,&stageLabel})
        if (c != nullptr) addAndMakeVisible(c);
    heading.setText("SONICATOR",juce::dontSendNotification);
    heading.setFont(juce::FontOptions(24,juce::Font::bold));
    subtitle.setText("INTELLIGENT SAMPLE EXPLORER",juce::dontSendNotification);
    subtitle.setFont(juce::FontOptions(11)); subtitle.setColour(juce::Label::textColourId,theme::muted);
    const bool branded = logoWordmark.isValid();
    heading.setVisible(! branded); subtitle.setVisible(! branded);
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
    search.setTextToShowWhenEmpty("Search filenames in this page...",theme::muted);
    search.onTextChange = [this] { library.setSearchText(search.getText()); };
    library.setComponentID("library"); status.setComponentID("importStatus"); analysisDetails.setComponentID("analysisDetails");
    library.onSelected = [this](const SampleRecord& r) { selectSample(r); };
    addAndMakeVisible(folders); rootsLabel.setVisible(false);
    folders.onFolder = [this](juce::String path) { selectedFolder = std::move(path); pageOffset = 0; reloadLibrary(); };
    addFolder.onClick = [this] { chooseFolder(); };
    rescan.onClick = [this] { scanRoots(false); }; reanalyze.onClick = [this] { scanRoots(true); };
    pause.onClick = [this] { if (indexer) indexer->setPaused(! indexer->progress().paused); };
    cancel.onClick = [this] { if (indexer) indexer->cancel(); };
    settings.onClick = [this] { showAudioSettings(); };
    previousPage.onClick = [this] { pageOffset = juce::jmax(0,pageOffset-10000); reloadLibrary(); };
    nextPage.onClick = [this] { pageOffset += 10000; reloadLibrary(); };
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
    addFolder.setEnabled(indexer != nullptr); rescan.setEnabled(indexer != nullptr); reanalyze.setEnabled(indexer != nullptr);
    setSize(1280,800); startTimerHz(10);
}
MainComponent::~MainComponent()
{
    stopTimer(); chooser.reset(); audioDialog.deleteAndZero(); indexer.reset(); setLookAndFeel(nullptr);
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
void MainComponent::scanRoots(bool force)
{
    if (! indexer || indexer->progress().running) return;
    try
    {
        auto roots = readRepo->roots();
        if (roots.isEmpty()) { status.setText("Add a folder first.",juce::dontSendNotification); return; }
        if (indexer->start(roots,force)) { wasRunning = true; reloadLibrary(); }
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
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
        catalogCount = readRepo->countInFolder(selectedFolder);
        if (pageOffset >= catalogCount) pageOffset = 0;
        auto samples = readRepo->pageInFolder(selectedFolder,10000,pageOffset);
        int shown = (int) samples.size(); library.setSamples(std::move(samples));
        countLabel.setText(juce::String(catalogCount) + " samples  |  " + juce::String(shown)
            + " on page " + juce::String(pageOffset/10000+1),juce::dontSendNotification);
        rootsLabel.setText(readRepo->roots().isEmpty() ? "No sample folders yet.\n\nOriginal files stay untouched."
            : "FOLDERS\n\n" + readRepo->roots().joinIntoString("\n\n"),juce::dontSendNotification);
        previousPage.setEnabled(pageOffset > 0); nextPage.setEnabled(pageOffset+10000 < catalogCount);
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
void MainComponent::selectSample(const SampleRecord& r)
{
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
    if (! preview.loadFile(juce::File(r.filePath))) status.setText(engine.error(),juce::dontSendNotification);
}
void MainComponent::timerCallback()
{
    if (! indexer) return;
    auto p = indexer->progress();
    pause.setEnabled(p.running); cancel.setEnabled(p.running);
    addFolder.setEnabled(! p.running); rescan.setEnabled(! p.running); reanalyze.setEnabled(! p.running);
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
}
void MainComponent::showAudioSettings()
{
    if (audioDialog != nullptr) { audioDialog->toFront(true); return; }
    auto* selector = new juce::AudioDeviceSelectorComponent(engine.deviceManager(),0,0,0,2,true,false,true,false);
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
    g.setColour(theme::accent);
    if (! headerIcon.isValid()) { g.drawEllipse(20,23,28,28,1.5f); g.drawEllipse(14,31,40,12,1.0f); }
    if (headerIcon.isValid())
        g.drawImage(headerIcon, juce::Rectangle<float>(16,26,32,32), juce::RectanglePlacement::centred);
    if (logoWordmark.isValid())
        g.drawImage(logoWordmark, juce::Rectangle<float>(58,10,230,66),
                    juce::RectanglePlacement::onlyReduceInSize | juce::RectanglePlacement::centred);
}
void MainComponent::resized()
{
    auto r = getLocalBounds(); headerBounds = r.removeFromTop(86);
    auto header = headerBounds.reduced(16,14); header.removeFromLeft(46);
    auto branding = header.removeFromLeft(280);
    heading.setBounds(branding.removeFromTop(30)); subtitle.setBounds(branding);
    settings.setBounds(header.removeFromRight(115).withSizeKeepingCentre(115,32));
    header.removeFromRight(16); search.setBounds(header.withSizeKeepingCentre(header.getWidth(),34));
    preview.setBounds(r.removeFromBottom(156));
    auto footer = r.removeFromBottom(42).reduced(12,5);
    cancel.setBounds(footer.removeFromRight(70)); footer.removeFromRight(6);
    pause.setBounds(footer.removeFromRight(70)); status.setBounds(footer);
    sidebarBounds = r.removeFromLeft(getWidth() < 1100 ? 190 : 240); analysisBounds = r.removeFromRight(getWidth() < 1100 ? 230 : 270);
    auto sidebar = sidebarBounds.reduced(14,16);
    libraryHeading.setBounds(sidebar.removeFromTop(30)); sidebar.removeFromTop(12);
    addFolder.setBounds(sidebar.removeFromTop(36)); sidebar.removeFromTop(10);
    rescan.setBounds(sidebar.removeFromTop(32)); sidebar.removeFromTop(8);
    reanalyze.setBounds(sidebar.removeFromTop(32)); sidebar.removeFromTop(24);
    folders.setBounds(sidebar);
    auto analysis = analysisBounds.reduced(16,16);
    analysisHeading.setBounds(analysis.removeFromTop(30)); analysis.removeFromTop(12);
    stageLabel.setBounds(analysis.removeFromBottom(getHeight() < 700 ? 58 : 90));
    analysisViewport.setBounds(analysis);
    analysisDetails.setBounds(0,0,juce::jmax(1,analysis.getWidth()-14),550);
    auto list = r.reduced(14,14); auto bar = list.removeFromTop(32);
    nextPage.setBounds(bar.removeFromRight(30)); bar.removeFromRight(4);
    previousPage.setBounds(bar.removeFromRight(30)); countLabel.setBounds(bar);
    list.removeFromTop(12); library.setBounds(list);
}
}
