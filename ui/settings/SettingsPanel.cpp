#include "SettingsPanel.h"
#include "HotkeysPanel.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/shell/Theme.h"
#include "ui/shell/DesignTokens.h"
#include "core/Version.h"
#include "core/diagnostics/Diagnostics.h"
#include "core/smart/AcousticClassifierService.h"
#include "ui/shell/ThemedDialogs.h"
#include <algorithm>

namespace sonicator {
namespace {
constexpr int labelColumn = 150;

juce::Font sectionFont() { return studio::sectionFont(); }
juce::Font noteFont() { return studio::captionFont(); }

struct Row
{
    int height = 0, gap = 0;
    std::function<void(juce::Rectangle<int>)> place;
};
using Rows = std::vector<Row>;

void placeRows(juce::Component& content, int width, const Rows& rows, int minimumHeight,
               std::vector<juce::Rectangle<float>>* rules)
{
    int height = 0;
    for (const auto& row : rows) height += row.height + row.gap;
    content.setSize(width, juce::jmax(minimumHeight, height));
    int y = 0;
    for (const auto& row : rows)
    {
        y += row.gap;
        row.place({ 0, y, width, row.height });
        y += row.height;
    }
    juce::ignoreUnused(rules);
}
}

SettingsPanel::SettingsPanel(const AppSettings& initial, juce::File file, bool busy)
    : current(initial), database(std::move(file)), indexing(busy),
      gumroad("ilyaorange.gumroad.com", juce::URL("https://ilyaorange.gumroad.com")),
      bandcamp("ilyaorange.bandcamp.com", juce::URL("https://ilyaorange.bandcamp.com")),
      naukograd("naukograd.bandcamp.com", juce::URL("https://naukograd.bandcamp.com")),
      kofi("ko-fi.com/ilyaorange", juce::URL("https://ko-fi.com/ilyaorange")),
      boosty("boosty.to/ilyaorange", juce::URL("https://boosty.to/ilyaorange"))
{
    setWantsKeyboardFocus(true);

    // --- general page controls -------------------------------------------------------------
    for (auto* c : std::array<juce::Component*, 23> {
             &appearance, &themeLabel, &scaleLabel, &languageHeading, &languageLabel, &languageNote,
             &libraryHeading, &databaseKey, &databaseLabel, &libraryNote, &databaseStatus,
             &tagsHeading, &roundTagMenuToggle, &roundTagMenuNote,
             &editingHeading, &alwaysOverwriteToggle, &alwaysOverwriteNote,
             &workspaceHeading, &workspaceNote, &themeBox, &scaleBox, &languageBox, &moveDatabase })
        generalContent.addAndMakeVisible(c);
    for (auto* c : std::array<juce::Component*, 8> {
             &openDatabase, &backupDatabase, &relocateDatabase, &contributeDataToggle,
             &contributionNote, &resetLayout, &overwriteDisposalLabel, &overwriteDisposalBox })
        generalContent.addAndMakeVisible(c);
    generalContent.addAndMakeVisible(contributionPreview);
    generalContent.addAndMakeVisible(contributionDelete);
    contributionStatus.setFont(noteFont());
    contributionStatus.setJustificationType(juce::Justification::centredLeft);
    contributionStatus.setColour(juce::Label::textColourId, theme::muted);
    contributionStatus.setVisible(false);
    generalContent.addAndMakeVisible(contributionStatus);
    audioContent.addAndMakeVisible(audio);
    audioHeading.setText("AUDIO & MIDI", juce::dontSendNotification);
    audioHeading.setFont(sectionFont());
    audioHeading.setColour(juce::Label::textColourId, theme::muted);
    audioContent.addAndMakeVisible(audioHeading);
    audioContent.addAndMakeVisible(audioNote);
    audioNote.setFont(noteFont());
    audioNote.setJustificationType(juce::Justification::topLeft);
    audioNote.setColour(juce::Label::textColourId, theme::muted);
    audioNote.setText("Choose the audio device and MIDI input here. In the DAW plugin the host owns "
                      "the devices, so this page is hidden.", juce::dontSendNotification);

    // --- about page controls ---------------------------------------------------------------
    for (auto* c : std::array<juce::Component*, 20> {
             &reportHeading, &reportNote, &aboutHeading, &productLabel, &authorLabel,
             &versionLabel, &checkUpdates, &releasesLink, &updateStatus,
             &linksHeading, &supportHeading, &supportNote, &creditsHeading, &creditsText,
             &prototypesLabel, &reportBug, &reportFeature, &reportFormat, &reportDiscuss,
             &gumroad })
        aboutContent.addAndMakeVisible(c);
    aboutContent.addAndMakeVisible(bandcamp);
    aboutContent.addAndMakeVisible(naukograd);
    aboutContent.addAndMakeVisible(kofi);
    aboutContent.addAndMakeVisible(boosty);

    addAndMakeVisible(title);
    title.setText("Settings", juce::dontSendNotification);
    title.setFont(studio::pageTitleFont());

    const auto pendingCrash = diagnostics::pendingCrash(diagnostics::defaultCrashMarker(), database);
    reportHeading.setText("REPORT", juce::dontSendNotification); reportHeading.setFont(sectionFont());
    reportNote.setText(juce::String("Opens a new issue in your browser with the app version, the OS and the "
                                    "tail of the log. Your home folder and catalog path are shortened; nothing "
                                    "is sent automatically.")
                       + (pendingCrash.isNotEmpty() ? " The previous session ended unexpectedly and its crash "
                                                      "trace will be included." : ""),
                       fonts::font(12.5f), theme::muted, 4);
    const auto openReport = [this](diagnostics::ReportKind kind)
    {
        diagnostics::ReportContext context;
        context.appVersion = version();
        context.catalogFile = database;
        if (juce::URL(diagnostics::issueUrl(kind, context)).launchInDefaultBrowser())
            diagnostics::clearPendingCrash(diagnostics::defaultCrashMarker());
    };
    reportBug.onClick = [openReport] { openReport(diagnostics::ReportKind::bug); };
    reportFeature.onClick = [openReport] { openReport(diagnostics::ReportKind::feature); };
    reportFormat.onClick = [openReport] { openReport(diagnostics::ReportKind::formatSupport); };
    reportDiscuss.onClick = []
    {
        juce::URL("https://github.com/orange303-afk/sonicator/discussions/new").launchInDefaultBrowser();
    };

    contributeDataToggle.setButtonText("Contribute anonymous feedback to improve classification models");
    contributeDataToggle.setToggleState(initial.contributeTrainingData, juce::dontSendNotification);
    contributionNote.setText("Sends confirmed types, tags, tempo and key plus acoustic numbers (14 values, "
                             "or 84 on rows with the richer vector). No audio, "
                             "file names or paths. Off by default.", juce::dontSendNotification);
    contributionNote.setFont(noteFont());
    contributionNote.setJustificationType(juce::Justification::centredLeft);
    contributeDataToggle.onClick = [this]
    {
        if (onContributionToggle) onContributionToggle(contributeDataToggle.getToggleState());
    };
    contributionPreview.onClick = [this] { showContributionPreview(); };
    contributionDelete.onClick = [this] { confirmContributionDelete(); };

    appearance.setText("APPEARANCE", juce::dontSendNotification); appearance.setFont(sectionFont());
    themeLabel.setText("Theme", juce::dontSendNotification); themeLabel.setFont(studio::controlFont());
    scaleLabel.setText("Interface scale", juce::dontSendNotification); scaleLabel.setFont(studio::controlFont());
    themeBox.setComponentID("settingsTheme");
    scaleBox.setComponentID("settingsScale");
    languageBox.setComponentID("settingsLanguage");
    themeBox.addItem("System", 1); themeBox.addItem("Dark", 2); themeBox.addItem("Light", 3);
    themeBox.setSelectedId(initial.theme == ThemeMode::Dark ? 2 : (initial.theme == ThemeMode::Light ? 3 : 1),
                           juce::dontSendNotification);
    scaleBox.addItem("100%", 100); scaleBox.addItem("110%", 110); scaleBox.addItem("125%", 125);
    scaleBox.addItem("150%", 150);
    scaleBox.setSelectedId(AppSettings::clampScale(initial.scalePercent), juce::dontSendNotification);
    languageHeading.setText("LANGUAGE", juce::dontSendNotification); languageHeading.setFont(sectionFont());
    languageLabel.setText("Interface language", juce::dontSendNotification);
    languageLabel.setFont(studio::controlFont());
    languageNote.setText("English is the only language in this stage. The choice is stored so later "
                         "translations can use it.", juce::dontSendNotification);
    languageNote.setFont(noteFont()); languageNote.setJustificationType(juce::Justification::centredLeft);
    languageBox.addItem("English", 1); languageBox.setSelectedId(1, juce::dontSendNotification);
    languageBox.setEnabled(false);
    libraryHeading.setText("LIBRARY", juce::dontSendNotification); libraryHeading.setFont(sectionFont());
    databaseKey.setText("Database", juce::dontSendNotification); databaseKey.setFont(studio::controlFont());
    databaseLabel.setFont(studio::captionFont());
    databaseLabel.setJustificationType(juce::Justification::centredLeft);
    databaseLabel.setBorderSize(juce::BorderSize<int>(0, 10, 0, 10));
    databaseLabel.setMinimumHorizontalScale(0.8f);
    libraryNote.setFont(noteFont()); libraryNote.setJustificationType(juce::Justification::centredLeft);
    databaseStatus.setFont(noteFont()); databaseStatus.setJustificationType(juce::Justification::centredLeft);
    tagsHeading.setText("TAGS", juce::dontSendNotification); tagsHeading.setFont(sectionFont());
    tagsHeading.setColour(juce::Label::textColourId, theme::muted);
    roundTagMenuToggle.setComponentID("settingsRoundTagMenu");
    roundTagMenuToggle.setToggleState(initial.roundTagMenu, juce::dontSendNotification);
    roundTagMenuToggle.onClick = [this]
    {
        current.roundTagMenu = roundTagMenuToggle.getToggleState();
        notifySettings();
    };
    roundTagMenuNote.setText("Shows one Tags pill at the top of the row menu; hovering or clicking it opens "
                             "the round tag menu. Ctrl/Cmd+T opens the same menu for the selected rows "
                             "or folder. Off keeps the current tag list.",
                             juce::dontSendNotification);
    roundTagMenuNote.setFont(noteFont());
    roundTagMenuNote.setJustificationType(juce::Justification::centredLeft);
    editingHeading.setText("EDITING", juce::dontSendNotification); editingHeading.setFont(sectionFont());
    alwaysOverwriteToggle.setComponentID("settingsAlwaysOverwrite");
    alwaysOverwriteToggle.setToggleState(initial.alwaysOverwriteOriginalFiles, juce::dontSendNotification);
    alwaysOverwriteToggle.onClick = [this]
    {
        current.alwaysOverwriteOriginalFiles = alwaysOverwriteToggle.getToggleState();
        overwriteDisposalBox.setEnabled(current.alwaysOverwriteOriginalFiles);
        notifySettings();
    };
    alwaysOverwriteNote.setText("Save and Normalize selected replace the file without asking. "
                                "WAV, AIFF and FLAC only; other formats are saved as a new copy. "
                                "The prompt for unsaved edits before leaving a file stays. "
                                "Replaced originals move to Trash unless Delete permanently is chosen.",
                                juce::dontSendNotification);
    alwaysOverwriteNote.setFont(noteFont());
    alwaysOverwriteNote.setJustificationType(juce::Justification::centredLeft);
    overwriteDisposalLabel.setText("Replaced originals", juce::dontSendNotification);
    overwriteDisposalLabel.setFont(noteFont());
    overwriteDisposalLabel.setJustificationType(juce::Justification::centredLeft);
    overwriteDisposalBox.setComponentID("settingsOverwriteDisposal");
    overwriteDisposalBox.addItem(files::disposalPolicyLabel(files::DisposalPolicy::Trash), 1);
    overwriteDisposalBox.addItem(files::disposalPolicyLabel(
                                     files::DisposalPolicy::DeletePermanently), 2);
    overwriteDisposalBox.setSelectedId(
        initial.overwriteOriginalDisposal == files::DisposalPolicy::DeletePermanently ? 2 : 1,
        juce::dontSendNotification);
    overwriteDisposalBox.setEnabled(initial.alwaysOverwriteOriginalFiles);
    overwriteDisposalBox.setTooltip("Delete permanently is not recoverable through Trash. "
                                    "Delete from disk and duplicate cleanup always ask separately.");
    overwriteDisposalBox.onChange = [this]
    {
        current.overwriteOriginalDisposal = overwriteDisposalBox.getSelectedId() == 2
            ? files::DisposalPolicy::DeletePermanently : files::DisposalPolicy::Trash;
        notifySettings();
    };
    privacyHeading.setText("PRIVACY & DATA", juce::dontSendNotification);
    privacyHeading.setFont(sectionFont());
    privacyHeading.setColour(juce::Label::textColourId, theme::muted);
    generalContent.addAndMakeVisible(privacyHeading);
    workspaceHeading.setText("WORKSPACE", juce::dontSendNotification); workspaceHeading.setFont(sectionFont());
    workspaceNote.setText("Restores the folder and analysis panel widths and the preview height. "
                          "The window size, table columns and shortcuts are not changed.",
                          juce::dontSendNotification);
    workspaceNote.setFont(noteFont()); workspaceNote.setJustificationType(juce::Justification::centredLeft);
    resetLayout.onClick = [this] { if (onResetLayout) onResetLayout(); };

    aboutHeading.setText("ABOUT", juce::dontSendNotification); aboutHeading.setFont(sectionFont());
    productLabel.setText("Sonicator", juce::dontSendNotification);
    productLabel.setFont(fonts::font(24.0f, fonts::Weight::semiBold));
    authorLabel.setText("by Ilya Orange", juce::dontSendNotification);
    authorLabel.setFont(fonts::font(14.0f));
    authorLabel.setColour(juce::Label::textColourId, theme::muted);
    versionLabel.setText("Version " + juce::String(version()), juce::dontSendNotification);
    versionLabel.setFont(fonts::font(12.5f));
    versionLabel.setColour(juce::Label::textColourId, theme::muted);
    checkUpdates.setTooltip("Asks github.com for the newest published release");
    checkUpdates.onClick = [this] { startUpdateCheck(); };
    checkUpdates.setComponentID("settingsCheckUpdates");
    releasesLink.setComponentID("settingsReleaseLink");
    releasesLink.setFont(fonts::font(12.5f), false);
    releasesLink.setJustificationType(juce::Justification::centredLeft);
    releasesLink.setVisible(false);
    linksHeading.setText("BY THE AUTHOR", juce::dontSendNotification);
    linksHeading.setFont(sectionFont());
    linksHeading.setColour(juce::Label::textColourId, theme::muted);
    supportHeading.setText("SUPPORT", juce::dontSendNotification);
    supportHeading.setFont(sectionFont());
    supportHeading.setColour(juce::Label::textColourId, theme::muted);
    supportNote.setText("Sonicator is free. You can help support further development.",
                        fonts::font(13.0f), theme::muted, 3);
    creditsHeading.setText("TECHNOLOGY & CREDITS", juce::dontSendNotification);
    creditsHeading.setFont(sectionFont());
    creditsText.setText(juce::String::fromUTF8(
        "JUCE 9.0.5  ·  SQLite 3.47.2  ·  libxmp 4.6.2  ·  libsamplerate 0.2.2  ·  LAME 3.100  ·  ONNX Runtime 1.20.1  ·  Catch2 (tests)\n"
        "\n"
        "Analysis: own DSP - YIN pitch, chroma key, spectral-flux rhythm, adaptive prototypes. "
        "REX2 decoder based on VelociLoops (Unlicense).\n"
        "\n"
        "No telemetry. Training data leaves only with your consent. No pretrained weights.\n"
        "\n"
        "Model trained on NSynth, Freesound One-Shot Percussive Sounds, FSD50K, ESC-50 and UrbanSound8K; "
        "see docs/DATA_CREDITS.md.\n"
        "\n"
        "Licensed under AGPL-3.0-or-later."),
        fonts::font(12.5f), theme::muted, 5);
    {
        const auto& classifier = AcousticClassifierService::getSharedAdaptiveClassifier();
        const auto generatedAt = classifier.globalGeneratedAt();
        const auto kind = classifier.hasGlobalModel() ? "Global model" : "Global prototypes";
        prototypesLabel.setText(generatedAt.isNotEmpty()
                                    ? juce::String(kind) + ": " + generatedAt.substring(0, 10) + ", "
                                          + juce::String(classifier.globalLabelCount()) + " labels"
                                    : juce::String(kind) + ": none in this build",
                                juce::dontSendNotification);
        prototypesLabel.setFont(fonts::font(12.5f));
        prototypesLabel.setJustificationType(juce::Justification::centredLeft);
        prototypesLabel.setColour(juce::Label::textColourId, theme::muted);
    }
    for (auto* link : std::array<juce::HyperlinkButton*, 5> { &gumroad, &bandcamp, &naukograd, &kofi, &boosty })
    {
        link->setFont(fonts::font(13.0f), false);
        link->setJustificationType(juce::Justification::centredLeft);
    }
    for (auto* button : std::array<juce::Button*, 12> { &moveDatabase, &openDatabase, &relocateDatabase,
                                                       &backupDatabase, &audio, &reportBug, &reportFeature,
                                                       &reportFormat, &reportDiscuss, &close,
                                                       &contributionPreview, &contributionDelete })
        button->setWantsKeyboardFocus(true);
    moveDatabase.setTooltip("Copy the current catalog through SQLite and switch to the copy");
    openDatabase.setTooltip("Open an existing SONICATOR catalog without moving the current one");
    relocateDatabase.setTooltip("Rewrite catalog paths for folders that moved to this computer");
    backupDatabase.setTooltip("Save a consistent copy of the catalog database; original audio files are not included");
    resetLayout.setTooltip("Restore the default folder width, analysis width and preview height");
    close.setVisible(false);
    close.setTooltip("Close");
    setDatabaseFile(database);
    setDatabaseStatus(indexing ? "Unavailable while indexing." : "Original audio files are not included in the database.", false);

    themeBox.onChange = [this]
    {
        current.theme = themeBox.getSelectedId() == 2 ? ThemeMode::Dark
            : (themeBox.getSelectedId() == 3 ? ThemeMode::Light : ThemeMode::System);
        notifySettings();
    };
    scaleBox.onChange = [this] { current.scalePercent = AppSettings::clampScale(scaleBox.getSelectedId()); notifySettings(); };
    languageBox.onChange = [this] { current.language = "English"; notifySettings(); };
    moveDatabase.onClick = [this] { chooseDatabase(true); };
    openDatabase.onClick = [this] { chooseDatabase(false); };
    relocateDatabase.onClick = [this] { if (onRelocateRequested) onRelocateRequested(); };
    backupDatabase.onClick = [this]
    {
        if (!onBackupRequested) return;
        juce::String error;
        const auto file = onBackupRequested(error);
        if (file.existsAsFile()) setDatabaseStatus("Backup saved: " + file.getFullPathName(), false);
        else setDatabaseStatus("Backup failed: " + (error.isNotEmpty() ? error : juce::String("unknown error")), true);
    };
    audio.onClick = [this] { if (onShowAudio) onShowAudio(); };
    close.onClick = [this]
    {
        if (onClose) { onClose(); return; }
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) dialog->exitModalState(0);
    };

    // --- navigation and pages ---------------------------------------------------------------
    const std::array<juce::String, 5> labels { "General", "Audio & MIDI", "Hotkeys", "Plugins", "About" };
    for (size_t i = 0; i < nav.size(); ++i)
    {
        auto& button = nav[i];
        button.setButtonText(labels[i]);
        button.setClickingTogglesState(true);
        button.setRadioGroupId(0x5e771e);
        button.setWantsKeyboardFocus(true);
        button.getProperties().set("leftAligned", true);
        button.setColour(juce::TextButton::buttonColourId, theme::panel);
        button.setColour(juce::TextButton::buttonOnColourId, theme::raised);
        button.setComponentID("settingsNav" + settingsSectionId((SettingsSection) i));
        button.onClick = [this, i]
        {
            const auto target = (SettingsSection) i;
            if (sectionAvailable(target)) selectSection(target);
            else nav[(size_t) section].setToggleState(true, juce::dontSendNotification);
        };
        addAndMakeVisible(button);
    }

    generalViewport.setViewedComponent(&generalContent, false);
    generalViewport.setScrollBarsShown(true, false);
    aboutViewport.setViewedComponent(&aboutContent, false);
    aboutViewport.setScrollBarsShown(true, false);
    addChildComponent(generalViewport);
    addChildComponent(aboutViewport);

    pluginsHeading.setText("PLUGINS", juce::dontSendNotification);
    pluginsHeading.setFont(sectionFont());
    pluginsHeading.setColour(juce::Label::textColourId, theme::muted);
    pluginsNote.setText("User components arrive in a later stage. This page will install, enable, update "
                        "and remove them. Nothing is installed today and no third-party code runs.",
                        juce::dontSendNotification);
    pluginsNote.setFont(noteFont());
    pluginsNote.setJustificationType(juce::Justification::topLeft);
    pluginsNote.setColour(juce::Label::textColourId, theme::muted);
    pluginsContent.addAndMakeVisible(pluginsHeading);
    pluginsContent.addAndMakeVisible(pluginsNote);
    addChildComponent(pluginsContent);
    addChildComponent(audioContent);

    addAndMakeVisible(title);
    addAndMakeVisible(close);

    section = settingsSectionFromId(initial.lastSettingsSection);
    if (!sectionAvailable(section)) section = SettingsSection::general;
    nav[(size_t) section].setToggleState(true, juce::dontSendNotification);
    updateSectionVisibility();
    setBusyState(busy);
    // The default size is set last: the About page measures its paragraphs, so their text has to
    // exist before the first layout runs.
    setSize(760, 640);
}

SettingsPanel::~SettingsPanel() = default;

void SettingsPanel::setBusyState(bool busy)
{
    indexing = busy;
    catalogBusy = busy;
    moveDatabase.setEnabled(!busy);
    openDatabase.setEnabled(!busy);
    relocateDatabase.setEnabled(!busy);
    libraryNote.setText(busy
        ? "Indexing is running. Stop it before moving or opening another catalog."
        : "Move copies a consistent snapshot to a new location and switches to it; the old file is kept. "
          "Open uses an existing catalog.", juce::dontSendNotification);
    if (!busy && databaseStatus.getText().containsIgnoreCase("Unavailable"))
        setDatabaseStatus("Original audio files are not included in the database.", false);
}

void SettingsPanel::setCommandRegistry(const CommandRegistry* commands)
{
    registry = commands;
    if (commands == nullptr) return;
    hotkeys = std::make_unique<HotkeysPanel>(*commands);
    hotkeys->setComponentID("hotkeysPanel");
    hotkeys->setProfile(profile);
    hotkeys->onProfileChanged = [this](const ShortcutProfile& candidate)
    {
        const bool saved = onShortcutProfileChanged == nullptr || onShortcutProfileChanged(candidate);
        if (saved) profile = candidate;
        return saved;
    };
    hotkeys->onConfirmResetAll = [this](std::function<void(bool)> done)
    {
        dialogs::showAlert(*this, "Reset all shortcuts?",
            "Every command returns to its default shortcut. Your own bindings are lost.",
            { { "Cancel", 0 }, { "Reset all", 1 } }, [done](int result) { done(result == 1); });
    };
    addChildComponent(*hotkeys);
    updateSectionVisibility();
    resized();
}

void SettingsPanel::setShortcutDiagnostics(const juce::StringArray& diagnostics)
{
    if (hotkeys != nullptr) hotkeys->setDiagnostics(diagnostics);
}

void SettingsPanel::setShortcutProfile(const ShortcutProfile& next)
{
    profile = next;
    if (hotkeys != nullptr) hotkeys->setProfile(profile);
}

void SettingsPanel::setAudioSettingsFactory(std::function<std::unique_ptr<juce::Component>()> factory)
{
    audioFactory = std::move(factory);
    audioPageBuilt = false;
    if (audioPage != nullptr) audioContent.removeChildComponent(audioPage);
    audioPage = nullptr;
    audioOwned.reset();
    if (section == SettingsSection::audioMidi) ensureAudioPage();
}

void SettingsPanel::ensureAudioPage()
{
    if (audioPageBuilt) return;
    audioPageBuilt = true;
    if (audioFactory == nullptr) return;
    audioOwned = audioFactory();
    if (audioOwned == nullptr) return;
    audioPage = audioOwned.get();
    audioOwned->setComponentID("settingsAudioPage");
    audioContent.addAndMakeVisible(*audioOwned);
    audio.setVisible(false); // the page is the embedded selector; the legacy button is a fallback
    audioPage->setVisible(section == SettingsSection::audioMidi);
    audioNote.setVisible(false);
}

bool SettingsPanel::sectionAvailable(SettingsSection candidate) const
{
    if (!pluginMode) return true;
    // The host owns audio, devices and any loaded code, so those pages do not appear.
    return candidate != SettingsSection::audioMidi && candidate != SettingsSection::plugins;
}

void SettingsPanel::selectSection(SettingsSection next)
{
    if (!sectionAvailable(next)) next = SettingsSection::general;
    if (next != SettingsSection::hotkeys && hotkeys != nullptr) hotkeys->cancelCapture();
    const bool changed = section != next;
    section = next;
    for (size_t i = 0; i < nav.size(); ++i)
        nav[i].setToggleState((SettingsSection) i == section, juce::dontSendNotification);
    if (section == SettingsSection::audioMidi) ensureAudioPage();
    updateSectionVisibility();
    resized();
    repaint();
    if (changed && onSectionChanged != nullptr) onSectionChanged(section);
}

void SettingsPanel::updateSectionVisibility()
{
    generalViewport.setVisible(section == SettingsSection::general);
    aboutViewport.setVisible(section == SettingsSection::about);
    audioContent.setVisible(section == SettingsSection::audioMidi);
    pluginsContent.setVisible(section == SettingsSection::plugins);
    if (hotkeys != nullptr) hotkeys->setVisible(section == SettingsSection::hotkeys);
    if (audioPage != nullptr) audioPage->setVisible(section == SettingsSection::audioMidi);
    audioNote.setVisible(section == SettingsSection::audioMidi && audioPage == nullptr);

    // Plugin mode hides the controls the host owns, not the whole General page.
    const bool showLibrary = !pluginMode;
    for (auto* c : std::array<juce::Component*, 14> {
             &libraryHeading, &databaseKey, &databaseLabel, &libraryNote,
             &databaseStatus, &moveDatabase, &openDatabase, &relocateDatabase, &backupDatabase,
             &contributeDataToggle, &contributionNote, &contributionStatus, &contributionPreview,
             &contributionDelete })
        if (!showLibrary) c->setVisible(false);
    if (showLibrary)
    {
        // Restore the ordinary state; the values are already owned by the controls.
        for (auto* c : std::array<juce::Component*, 14> {
                 &libraryHeading, &databaseKey, &databaseLabel, &libraryNote,
                 &databaseStatus, &moveDatabase, &openDatabase, &relocateDatabase, &backupDatabase,
                 &contributeDataToggle, &contributionNote, &contributionStatus, &contributionPreview,
                 &contributionDelete })
            c->setVisible(true);
        contributionStatus.setVisible(contributionStatus.getText().isNotEmpty());
    }
    close.setVisible(true);
    nav[(size_t) SettingsSection::audioMidi].setVisible(!pluginMode);
    nav[(size_t) SettingsSection::plugins].setVisible(!pluginMode);
}

void SettingsPanel::setPluginMode(bool shouldUsePluginMode)
{
    pluginMode = shouldUsePluginMode;
    if (!sectionAvailable(section)) section = SettingsSection::general;
    nav[(size_t) section].setToggleState(true, juce::dontSendNotification);
    updateSectionVisibility();
    resized();
    repaint();
}

void SettingsPanel::notifySettings()
{
    if (onSettingsChanged) onSettingsChanged(current);
    sendLookAndFeelChange();
    repaint();
}

void SettingsPanel::chooseDatabase(bool moveCurrent)
{
    if (indexing) { setDatabaseStatus("Stop indexing before switching catalogs.", true); return; }
    chooser = std::make_unique<juce::FileChooser>(moveCurrent ? "Copy catalog to..." : "Open catalog",
                                                 database.getParentDirectory(), "*.sqlite");
    const auto flags = moveCurrent
        ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting
        : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [safe = juce::Component::SafePointer<SettingsPanel>(this), moveCurrent](const juce::FileChooser& chooser)
    {
        if (safe == nullptr) return;
        const auto file = chooser.getResult();
        if (file == juce::File()) return;
        if (safe->onDatabaseChosen) safe->onDatabaseChosen(file, moveCurrent);
    });
}

void SettingsPanel::setDatabaseFile(juce::File file)
{
    database = std::move(file);
    databaseLabel.setText(database.getFullPathName(), juce::dontSendNotification);
    databaseLabel.setTooltip(database.getFullPathName());
}

void SettingsPanel::setDatabaseStatus(const juce::String& text, bool error)
{
    statusError = error;
    databaseStatus.setColour(juce::Label::textColourId, error
        ? (theme::isDark ? juce::Colour(0xffff8b8b) : juce::Colour(0xffb3261e)) : theme::muted);
    databaseStatus.setText(text, juce::dontSendNotification);
}

void SettingsPanel::lookAndFeelChanged()
{
    appearance.setColour(juce::Label::textColourId, theme::muted);
    languageHeading.setColour(juce::Label::textColourId, theme::muted);
    libraryHeading.setColour(juce::Label::textColourId, theme::muted);
    editingHeading.setColour(juce::Label::textColourId, theme::muted);
    workspaceHeading.setColour(juce::Label::textColourId, theme::muted);
    privacyHeading.setColour(juce::Label::textColourId, theme::muted);
    aboutHeading.setColour(juce::Label::textColourId, theme::muted);
    themeLabel.setColour(juce::Label::textColourId, theme::text);
    scaleLabel.setColour(juce::Label::textColourId, theme::text);
    languageLabel.setColour(juce::Label::textColourId, theme::text);
    databaseKey.setColour(juce::Label::textColourId, theme::text);
    languageNote.setColour(juce::Label::textColourId, theme::muted);
    libraryNote.setColour(juce::Label::textColourId, theme::muted);
    workspaceNote.setColour(juce::Label::textColourId, theme::muted);
    databaseStatus.setColour(juce::Label::textColourId, statusError
        ? (theme::isDark ? juce::Colour(0xffff8b8b) : juce::Colour(0xffb3261e)) : theme::muted);
    contributeDataToggle.setColour(juce::ToggleButton::textColourId, theme::text);
    contributionNote.setColour(juce::Label::textColourId, theme::muted);
    contributeDataToggle.setColour(juce::ToggleButton::tickColourId, theme::accent);
    authorLabel.setColour(juce::Label::textColourId, theme::muted);
    productLabel.setColour(juce::Label::textColourId, theme::text);
    title.setColour(juce::Label::textColourId, theme::text);
    versionLabel.setColour(juce::Label::textColourId, theme::muted);
    updateStatus.setTextColour(updateAvailable ? theme::accent : theme::muted);
    reportHeading.setColour(juce::Label::textColourId, theme::muted);
    reportNote.setTextColour(theme::muted);
    linksHeading.setColour(juce::Label::textColourId, theme::muted);
    supportHeading.setColour(juce::Label::textColourId, theme::muted);
    creditsHeading.setColour(juce::Label::textColourId, theme::muted);
    supportNote.setTextColour(theme::muted);
    creditsText.setTextColour(theme::muted);
    prototypesLabel.setColour(juce::Label::textColourId, theme::muted);
    pluginsHeading.setColour(juce::Label::textColourId, theme::muted);
    pluginsNote.setColour(juce::Label::textColourId, theme::muted);
    for (auto* link : std::array<juce::HyperlinkButton*, 6> { &gumroad, &bandcamp, &naukograd, &kofi,
                                                             &boosty, &releasesLink })
        link->setColour(juce::HyperlinkButton::textColourId, theme::accent);
    for (auto& button : nav)
    {
        button.setColour(juce::TextButton::textColourOffId, theme::muted);
        button.getProperties().set("leftAligned", true);
        button.setColour(juce::TextButton::buttonColourId, theme::panel);
        button.setColour(juce::TextButton::buttonOnColourId, theme::raised);
    }
    repaint();
}

void SettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    auto navArea = getLocalBounds().removeFromLeft(studio::navigationWidth);
    g.setColour(theme::panel);
    g.fillRect(navArea);
    g.setColour(theme::line);
    g.drawVerticalLine(navArea.getRight() - 1, 0.0f, (float) getHeight());
    // Decorations are laid out inside the scrolling page, so translate them into panel space.
    auto toPanel = [this](const juce::Component& page, juce::Rectangle<float> area)
    {
        const auto topLeft = getLocalPoint(&page, area.getTopLeft().roundToInt());
        return area.translated((float) topLeft.x - area.getX(), (float) topLeft.y - area.getY());
    };
    if (!databaseFieldBounds.isEmpty() && generalViewport.isVisible())
    {
        // The page scrolls, so a decoration translated into panel space must not escape the
        // viewport: without this clip a scrolled field or card paints over the navigation column.
        const juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(generalViewport.getBounds());
        const auto field = toPanel(generalContent, databaseFieldBounds.toFloat()).reduced(0.5f);
        g.setColour(theme::background);
        g.fillRoundedRectangle(field, studio::controlRadius);
        g.setColour(theme::line);
        g.drawRoundedRectangle(field, studio::controlRadius, 1.0f);
    }
    if (generalViewport.isVisible())
    {
        const juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(generalViewport.getBounds());
        g.setColour(theme::line);
        for (const auto& rule : sectionRules) g.fillRect(toPanel(generalContent, rule));
    }
    if (aboutViewport.isVisible())
    {
        const juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(aboutViewport.getBounds());
        for (const auto& card : aboutCards)
        {
            const auto area = toPanel(aboutContent, card).reduced(0.5f);
            g.setColour(theme::panel);
            g.fillRoundedRectangle(area, studio::panelRadius);
            g.setColour(theme::line.withAlpha(0.65f));
            g.drawRoundedRectangle(area, studio::panelRadius, 1.0f);
        }
        g.setColour(theme::line);
        for (const auto& rule : aboutRules) g.fillRect(toPanel(aboutContent, rule));
    }
}

void SettingsPanel::startUpdateCheck()
{
    if (updateInFlight) return;
    updateInFlight = true;
    updateAvailable = false;
    checkUpdates.setEnabled(false);
    checkUpdates.setButtonText("Checking...");
    releasesLink.setVisible(false);
    updateStatus.setText("Looking for the latest release on GitHub.", fonts::font(12.5f), theme::muted, 4);
    resized();

    // The request runs off the message thread; a stale answer is dropped because the panel may be
    // gone by the time it arrives.
    juce::Component::SafePointer<SettingsPanel> safe(this);
    juce::Thread::launch([safe]
    {
        juce::MessageManager::callAsync([safe, release = updates::checkForUpdate(version())]
        {
            if (safe != nullptr) safe->showUpdateResult(release);
        });
    });
}

void SettingsPanel::showUpdateResult(const updates::Release& release)
{
    updateInFlight = false;
    checkUpdates.setEnabled(true);
    checkUpdates.setButtonText("Check for updates");
    updateAvailable = release.published && release.newer;
    releasesLink.setVisible(false);

    if (release.error.isNotEmpty())
        updateStatus.setText("Could not check: " + release.error, fonts::font(12.5f), theme::muted, 4);
    else if (! release.published)
    {
        updateStatus.setText("No release has been published yet.", fonts::font(12.5f), theme::muted, 4);
        releasesLink.setButtonText("Releases on GitHub");
        releasesLink.setURL(juce::URL(updates::releasesPageUrl()));
        releasesLink.setVisible(true);
    }
    else if (release.newer)
    {
        updateStatus.setText("Version " + release.tag + " is available.", fonts::font(12.5f),
                             theme::accent, 4);
        releasesLink.setButtonText("Download " + release.tag);
        releasesLink.setURL(juce::URL(
            updates::downloadUrlFor(release, updates::currentPlatform())));
        releasesLink.setVisible(true);
    }
    else
        updateStatus.setText("You are up to date (" + release.tag + ").", fonts::font(12.5f),
                             theme::muted, 4);

    resized();
}

void SettingsPanel::setContributionEnabled(bool enabled)
{
    contributeDataToggle.setToggleState(enabled, juce::dontSendNotification);
}

void SettingsPanel::setContributionStatus(const juce::String& text)
{
    contributionStatus.setText(text, juce::dontSendNotification);
    contributionStatus.setVisible(text.isNotEmpty() && contributeDataToggle.isVisible());
    resized();
}

void SettingsPanel::showContributionPreview()
{
    // The window deletes itself when dismissed, so it cannot live on the stack.
    auto* window = new juce::AlertWindow("What will be sent",
        "This is the exact request the next contribution would make. Nothing has been sent.",
        juce::MessageBoxIconType::NoIcon);
    window->setLookAndFeel(&getLookAndFeel());
    window->addTextEditor("json", onContributionPreview ? onContributionPreview() : juce::String("{}"), {}, true);
    if (auto* editor = window->getTextEditor("json"))
    {
        editor->setMultiLine(true, true);
        editor->setReadOnly(true);
        editor->setScrollbarsShown(true);
    }
    window->addButton("Close", 1, juce::KeyPress(juce::KeyPress::escapeKey));
    window->setSize(600, 440);
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->enterModalState(true, nullptr, true);
}

void SettingsPanel::confirmContributionDelete()
{
    juce::Component::SafePointer<SettingsPanel> safe(this);
    dialogs::showAlert(*this, "Delete contributed data?",
        "Removes every batch this installation sent and issues a new anonymous id. Data already "
        "included in a released dataset or model cannot be recalled. Contribution is turned off.",
        { { "Cancel", 0 }, { "Delete", 1 } },
        [safe](int result)
        {
            if (safe == nullptr || result != 1) return;
            safe->contributionDelete.setEnabled(false);
            safe->contributionDelete.setButtonText("Deleting...");
            auto finish = [safe](bool ok, juce::String error)
            {
                if (safe == nullptr) return;
                safe->contributionDelete.setEnabled(true);
                safe->contributionDelete.setButtonText("Delete my contributed data...");
                dialogs::showAlert(*safe, ok ? "Data deleted" : "Could not delete",
                    ok ? "The batches are gone and a new id was issued. Contribution is off."
                       : "Nothing was changed: " + (error.isNotEmpty() ? error : juce::String("no response")),
                    { { "OK", 1 } }, {});
            };
            if (safe->onContributionDelete) safe->onContributionDelete(finish);
        });
}

void SettingsPanel::layoutGeneralPage(juce::Rectangle<int> area)
{
    sectionRules.clear();
    const int width = juce::jmax(320, area.getWidth() - 6);
    Rows rows;
    auto add = [&rows](int height, int gap, std::function<void(juce::Rectangle<int>)> place)
    {
        rows.push_back({ height, gap, std::move(place) });
    };
    auto section = [this, &add, &rows](juce::Label& label, int gapBefore)
    {
        const int index = (int) rows.size();
        add(16, gapBefore, [this, &label, index](juce::Rectangle<int> row)
        {
            label.setBounds(row);
            const auto textWidth = juce::GlyphArrangement::getStringWidthInt(label.getFont(), label.getText());
            const auto lineX = row.getX() + textWidth + 12;
            if (lineX < row.getRight())
                sectionRules.emplace_back((float) lineX, (float) row.getCentreY(),
                                          (float) (row.getRight() - lineX), 1.0f);
            juce::ignoreUnused(index);
        });
    };
    auto row = [&add](juce::Label& label, juce::Component& control, int gapBefore)
    {
        add(studio::controlHeight, gapBefore, [&label, &control](juce::Rectangle<int> area)
        {
            label.setBounds(area.removeFromLeft(labelColumn));
            control.setBounds(area.removeFromLeft(juce::jmin(220, area.getWidth())));
        });
    };
    auto note = [&add](juce::Label& label, int height, int gapBefore)
    {
        add(height, gapBefore, [&label](juce::Rectangle<int> area)
        {
            label.setBounds(area.withTrimmedLeft(labelColumn));
        });
    };

    section(appearance, 0);
    row(themeLabel, themeBox, studio::spacingTight);
    if (scaleLabel.isVisible()) row(scaleLabel, scaleBox, studio::spacingTight);
    section(languageHeading, studio::spacingSection);
    row(languageLabel, languageBox, studio::spacingTight);
    note(languageNote, 34, 6);

    if (libraryHeading.isVisible())
    {
        section(libraryHeading, studio::spacingSection);
        add(studio::controlHeight, studio::spacingTight, [this](juce::Rectangle<int> area)
        {
            databaseKey.setBounds(area.removeFromLeft(labelColumn));
            databaseFieldBounds = area;
            databaseLabel.setBounds(area.reduced(0, 3));
        });
        add(studio::controlHeight, 6, [this](juce::Rectangle<int> area)
        {
            auto right = area.withTrimmedLeft(labelColumn);
            const auto width = (right.getWidth() - 16) / 3;
            moveDatabase.setBounds(right.removeFromLeft(width));
            right.removeFromLeft(8);
            openDatabase.setBounds(right.removeFromLeft(width));
            right.removeFromLeft(8);
            relocateDatabase.setBounds(right);
        });
        add(studio::controlHeight, studio::spacingTight, [this](juce::Rectangle<int> area)
        {
            backupDatabase.setBounds(area.withTrimmedLeft(labelColumn).removeFromLeft(180));
        });
        note(libraryNote, 36, 6);
        note(databaseStatus, 22, 2);
    }

    section(tagsHeading, studio::spacingSection);
    add(26, studio::spacingTight, [this](juce::Rectangle<int> area)
    { roundTagMenuToggle.setBounds(area.withTrimmedLeft(labelColumn)); });
    note(roundTagMenuNote, 52, 2);

    section(editingHeading, studio::spacingSection);
    add(26, studio::spacingTight, [this](juce::Rectangle<int> area)
    { alwaysOverwriteToggle.setBounds(area.withTrimmedLeft(labelColumn)); });
    note(alwaysOverwriteNote, 58, 2);
    add(studio::controlHeight, studio::spacingTight, [this](juce::Rectangle<int> area)
    {
        overwriteDisposalLabel.setBounds(area.removeFromLeft(labelColumn));
        overwriteDisposalBox.setBounds(area.removeFromLeft(220));
    });

    section(workspaceHeading, studio::spacingSection);
    add(studio::controlHeight, studio::spacingTight, [this](juce::Rectangle<int> area)
    { resetLayout.setBounds(area.withTrimmedLeft(labelColumn).removeFromLeft(200)); });
    note(workspaceNote, 36, 2);

    if (contributeDataToggle.isVisible())
    {
        section(privacyHeading, studio::spacingSection);
        add(26, studio::spacingTight, [this](juce::Rectangle<int> area)
        { contributeDataToggle.setBounds(area.withTrimmedLeft(labelColumn)); });
        note(contributionNote, 34, 4);
        if (contributionStatus.isVisible())
            note(contributionStatus, 34, 2);
        add(studio::controlHeight, studio::spacingTight, [this](juce::Rectangle<int> area)
        {
            auto right = area.withTrimmedLeft(labelColumn);
            const auto width = (right.getWidth() - 8) / 2;
            contributionPreview.setBounds(right.removeFromLeft(width));
            right.removeFromLeft(8);
            contributionDelete.setBounds(right);
        });
    }
    add(studio::spacingWide, 0, [](juce::Rectangle<int>) {});

    placeRows(generalContent, width, rows, area.getHeight(), &sectionRules);
}

void SettingsPanel::layoutAboutPage(juce::Rectangle<int> area)
{
    aboutRules.clear();
    aboutCards.clear();
    const int width = juce::jmax(320, area.getWidth() - 6);
    Rows rows;
    auto add = [&rows](int height, int gap, std::function<void(juce::Rectangle<int>)> place)
    {
        rows.push_back({ height, gap, std::move(place) });
    };
    auto section = [this, &add, &rows](juce::Label& label, int gapBefore)
    {
        add(16, gapBefore, [this, &label](juce::Rectangle<int> row)
        {
            label.setBounds(row);
            const auto textWidth = juce::GlyphArrangement::getStringWidthInt(label.getFont(), label.getText());
            const auto lineX = row.getX() + textWidth + 12;
            if (lineX < row.getRight())
                aboutRules.emplace_back((float) lineX, (float) row.getCentreY(),
                                        (float) (row.getRight() - lineX), 1.0f);
        });
    };

    // About is a document page: every line starts at the page edge, and the two groups that carry
    // short lists sit side by side on a panel so they read as groups, not as a form.
    const int cardGap = studio::spacingWide;
    const int cardPadding = studio::spacingWide;
    const int cardWidth = (width - cardGap) / 2;
    const int captionHeight = 14;
    const int linkHeight = 22;
    const int supportTextHeight = supportNote.heightForWidth(cardWidth - 2 * cardPadding);
    const int cardHeight = 2 * cardPadding
        + juce::jmax(captionHeight + 10 + 3 * linkHeight,
                     captionHeight + 10 + supportTextHeight + studio::spacing + 2 * linkHeight);

    section(aboutHeading, 0);
    add(34, studio::spacingTight, [this](juce::Rectangle<int> row) { productLabel.setBounds(row); });
    add(studio::controlHeight, 0, [this](juce::Rectangle<int> row)
    {
        checkUpdates.setBounds(row.removeFromRight(176));
        row.removeFromRight(studio::spacingWide);
        versionLabel.setBounds(row);
    });
    add(21, 0, [this](juce::Rectangle<int> row) { authorLabel.setBounds(row); });
    add(updateStatus.heightForWidth(width), studio::spacingTight,
        [this](juce::Rectangle<int> row) { updateStatus.setBounds(row); });
    if (releasesLink.isVisible())
        add(20, 4, [this](juce::Rectangle<int> row) { releasesLink.setBounds(row); });

    add(cardHeight, studio::spacingSection,
        [this, width, cardWidth, cardGap, cardPadding, captionHeight, linkHeight, supportTextHeight]
        (juce::Rectangle<int> row)
    {
        const auto left = row.withWidth(cardWidth);
        const auto right = juce::Rectangle<int>(row.getX() + cardWidth + cardGap, row.getY(),
                                                width - cardWidth - cardGap, row.getHeight());
        aboutCards.push_back(left.toFloat());
        aboutCards.push_back(right.toFloat());

        auto links = left.reduced(cardPadding);
        linksHeading.setBounds(links.removeFromTop(captionHeight));
        links.removeFromTop(10);
        for (auto* link : { &gumroad, &bandcamp, &naukograd })
            link->setBounds(links.removeFromTop(linkHeight));

        auto support = right.reduced(cardPadding);
        supportHeading.setBounds(support.removeFromTop(captionHeight));
        support.removeFromTop(10);
        supportNote.setBounds(support.removeFromTop(supportTextHeight));

        support.removeFromTop(studio::spacing);
        kofi.setBounds(support.removeFromTop(linkHeight));
        boosty.setBounds(support.removeFromTop(linkHeight));
    });

    section(reportHeading, studio::spacingSection);
    add(studio::controlHeight, studio::spacingTight, [this, width](juce::Rectangle<int> row)
    {
        const auto buttonWidth = (width - studio::spacingTight) / 2;
        reportBug.setBounds(row.removeFromLeft(buttonWidth));
        row.removeFromLeft(studio::spacingTight);
        reportFeature.setBounds(row.removeFromLeft(buttonWidth));
    });
    add(studio::controlHeight, studio::spacingTight, [this, width](juce::Rectangle<int> row)
    {
        const auto buttonWidth = (width - studio::spacingTight) / 2;
        reportFormat.setBounds(row.removeFromLeft(buttonWidth));
        row.removeFromLeft(studio::spacingTight);
        reportDiscuss.setBounds(row.removeFromLeft(buttonWidth));
    });
    add(reportNote.heightForWidth(width), studio::spacingTight,
        [this](juce::Rectangle<int> row) { reportNote.setBounds(row); });

    section(creditsHeading, studio::spacingSection);
    add(creditsText.heightForWidth(width), studio::spacingTight,
        [this](juce::Rectangle<int> row) { creditsText.setBounds(row); });
    add(20, studio::spacing, [this](juce::Rectangle<int> row) { prototypesLabel.setBounds(row); });
    add(studio::spacingSection, 0, [](juce::Rectangle<int>) {});

    placeRows(aboutContent, width, rows, area.getHeight(), &aboutRules);
}

void SettingsPanel::resized()
{
    auto bounds = getLocalBounds();
    auto navArea = bounds.removeFromLeft(studio::navigationWidth).reduced(studio::spacing, studio::spacingWide);
    title.setBounds(navArea.removeFromTop(34));
    navArea.removeFromTop(studio::spacingWide);
    for (auto& button : nav)
    {
        if (!button.isVisible()) continue;
        button.setBounds(navArea.removeFromTop(32));
        navArea.removeFromTop(4);
    }
    if (close.isVisible())
    {
        close.setBounds(navArea.removeFromBottom(studio::controlHeight));
        navArea.removeFromBottom(studio::spacingTight);
    }

    auto content = bounds.reduced(studio::spacingSection, studio::spacingWide);
    const int contentWidth = content.getWidth();
    generalViewport.setBounds(content);
    aboutViewport.setBounds(content);
    audioContent.setBounds(content);
    pluginsContent.setBounds(content);
    if (hotkeys != nullptr) hotkeys->setBounds(content);

    layoutGeneralPage({ 0, 0, contentWidth - (generalContent.getHeight() > content.getHeight() ? 12 : 0),
                        content.getHeight() });
    layoutAboutPage({ 0, 0, contentWidth - (aboutContent.getHeight() > content.getHeight() ? 12 : 0),
                      content.getHeight() });

    // The Audio page hosts the stock selector (or the fallback button) inside the page body.
    auto audioBody = audioContent.getLocalBounds();
    audioHeading.setBounds(audioBody.removeFromTop(16));
    audioBody.removeFromTop(studio::spacingWide);
    if (audioPage != nullptr)
    {
        audioPage->setBounds(audioBody);
        audioNote.setVisible(false);
    }
    else
    {
        audioNote.setBounds(audioBody.removeFromTop(40));
        audioBody.removeFromTop(studio::spacing);
        audio.setBounds(audioBody.removeFromTop(studio::controlHeight).removeFromLeft(200));
    }

    auto pluginsBody = pluginsContent.getLocalBounds();
    pluginsHeading.setBounds(pluginsBody.removeFromTop(16));
    pluginsBody.removeFromTop(studio::spacingWide);
    pluginsNote.setBounds(pluginsBody.removeFromTop(80));
}

bool SettingsPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey && onClose != nullptr)
    {
        onClose();
        return true;
    }
    return false;
}
} // namespace sonicator
