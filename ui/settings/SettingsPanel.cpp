#include "SettingsPanel.h"
#include "ui/shell/Theme.h"
#include "core/Version.h"
#include "core/diagnostics/Diagnostics.h"
namespace sonicator {
namespace {
juce::Font sectionFont()
{
    return juce::Font(juce::FontOptions(10.5f, juce::Font::bold)).withExtraKerningFactor(0.12f);
}
juce::Font noteFont()
{
    return juce::Font(juce::FontOptions(12.0f));
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
    for (auto* c : std::array<juce::Component*,25>{&title,&appearance,&themeLabel,&scaleLabel,&languageHeading,
         &languageLabel,&languageNote,&libraryHeading,&databaseKey,&databaseLabel,&libraryNote,&databaseStatus,
         &audioHeading,&reportHeading,&reportNote,&aboutHeading,&productLabel,&authorLabel,&supportLabel,&creditsLabel,&themeBox,&scaleBox,&languageBox,&moveDatabase,&relocateDatabase})
        addAndMakeVisible(c);
    for (auto* c : std::array<juce::Component*,12>{&openDatabase,&backupDatabase,&audio,&reportBug,&reportFeature,
         &reportFormat,&close,&gumroad,&bandcamp,&naukograd,&kofi,&boosty})
        addAndMakeVisible(c);

    const auto pendingCrash = diagnostics::pendingCrash (diagnostics::defaultCrashMarker(), database);
    reportHeading.setText("REPORT", juce::dontSendNotification); reportHeading.setFont(sectionFont());
    reportNote.setText(juce::String("Opens a new issue in your browser with the app version, the OS and the "
                                    "tail of the log. Your home folder and catalog path are shortened; nothing "
                                    "is sent automatically.")
                       + (pendingCrash.isNotEmpty() ? " The previous session ended unexpectedly and its crash "
                                                      "trace will be included." : ""),
                       juce::dontSendNotification);
    reportNote.setFont(noteFont());
    reportNote.setJustificationType(juce::Justification::topLeft);
    const auto openReport = [this](diagnostics::ReportKind kind)
    {
        diagnostics::ReportContext context;
        context.appVersion = version();
        context.catalogFile = database;
        if (juce::URL (diagnostics::issueUrl (kind, context)).launchInDefaultBrowser())
            diagnostics::clearPendingCrash (diagnostics::defaultCrashMarker());
    };
    reportBug.onClick = [openReport] { openReport (diagnostics::ReportKind::bug); };
    reportFeature.onClick = [openReport] { openReport (diagnostics::ReportKind::feature); };
    reportFormat.onClick = [openReport] { openReport (diagnostics::ReportKind::formatSupport); };
    addAndMakeVisible(contributeDataToggle);
    contributeDataToggle.setButtonText("Contribute anonymous feedback to improve classification models");
    contributeDataToggle.setToggleState(initial.contributeTrainingData, juce::dontSendNotification);
    contributeDataToggle.onClick = [this]
    {
        current.contributeTrainingData = contributeDataToggle.getToggleState();
        notifySettings();
    };

    title.setText("Settings",juce::dontSendNotification);
    title.setFont(juce::FontOptions(20,juce::Font::bold));
    appearance.setText("APPEARANCE",juce::dontSendNotification); appearance.setFont(sectionFont());
    themeLabel.setText("Theme",juce::dontSendNotification); themeLabel.setFont(juce::FontOptions(13));
    scaleLabel.setText("Interface scale",juce::dontSendNotification); scaleLabel.setFont(juce::FontOptions(13));
    themeBox.setComponentID("settingsTheme");
    scaleBox.setComponentID("settingsScale");
    languageBox.setComponentID("settingsLanguage");
    themeBox.addItem("System",1); themeBox.addItem("Dark",2); themeBox.addItem("Light",3);
    themeBox.setSelectedId(initial.theme == ThemeMode::Dark ? 2 : (initial.theme == ThemeMode::Light ? 3 : 1),juce::dontSendNotification);
    scaleBox.addItem("100%",100); scaleBox.addItem("110%",110); scaleBox.addItem("125%",125); scaleBox.addItem("150%",150);
    scaleBox.setSelectedId(AppSettings::clampScale(initial.scalePercent),juce::dontSendNotification);
    languageHeading.setText("LANGUAGE",juce::dontSendNotification); languageHeading.setFont(sectionFont());
    languageLabel.setText("Interface language",juce::dontSendNotification); languageLabel.setFont(juce::FontOptions(13));
    languageNote.setText("English is the only language in this stage. The choice is stored so later translations can use it.",juce::dontSendNotification);
    languageNote.setFont(noteFont()); languageNote.setJustificationType(juce::Justification::centredLeft);
    languageBox.addItem("English",1); languageBox.setSelectedId(1,juce::dontSendNotification);
    languageBox.setEnabled(false);
    libraryHeading.setText("LIBRARY",juce::dontSendNotification); libraryHeading.setFont(sectionFont());
    databaseKey.setText("Database",juce::dontSendNotification); databaseKey.setFont(juce::FontOptions(13));
    databaseLabel.setFont(juce::FontOptions(12)); databaseLabel.setJustificationType(juce::Justification::centredLeft);
    databaseLabel.setBorderSize(juce::BorderSize<int>(0,10,0,10));
    databaseLabel.setMinimumHorizontalScale(0.8f);
    libraryNote.setText(indexing
        ? "Indexing is running. Stop it before moving or opening another catalog."
        : "Move copies a consistent snapshot to a new location and switches to it; the old file is kept. Open uses an existing catalog.",juce::dontSendNotification);
    libraryNote.setFont(noteFont()); libraryNote.setJustificationType(juce::Justification::centredLeft);
    databaseStatus.setFont(noteFont()); databaseStatus.setJustificationType(juce::Justification::centredLeft);
    audioHeading.setText("AUDIO",juce::dontSendNotification); audioHeading.setFont(sectionFont());
    aboutHeading.setText("ABOUT",juce::dontSendNotification); aboutHeading.setFont(sectionFont());
    productLabel.setText("Sonicator",juce::dontSendNotification);
    productLabel.setFont(juce::FontOptions(18,juce::Font::bold));
    authorLabel.setText("by Ilya Orange",juce::dontSendNotification); authorLabel.setFont(juce::FontOptions(13));
    supportLabel.setText("Sonicator is free, but you can support further development:",juce::dontSendNotification);
    supportLabel.setFont(noteFont()); supportLabel.setJustificationType(juce::Justification::centredLeft);
    creditsLabel.setText(juce::String::fromUTF8(
        "JUCE 9.0.5  ·  SQLite 3.47.2  ·  libxmp 4.6.2  ·  ONNX Runtime 1.20.1  ·  Catch2 (tests)\n"
        "Analysis: own DSP - YIN pitch, chroma key, spectral-flux rhythm, adaptive prototypes.\n"
        "No network, no pretrained model weights. REX2 decoder based on VelociLoops (Unlicense).\n"
        "Licensed under AGPL-3.0-or-later."),
        juce::dontSendNotification);
    creditsLabel.setFont(juce::FontOptions(10.5f));
    creditsLabel.setJustificationType(juce::Justification::topLeft);
    creditsLabel.setMinimumHorizontalScale(0.7f);
    for (auto* link : std::array<juce::HyperlinkButton*,5>{&gumroad,&bandcamp,&naukograd,&kofi,&boosty})
    {
        link->setFont(juce::FontOptions(12),false);
        link->setJustificationType(juce::Justification::centredLeft);
    }
    for (auto* button : std::array<juce::Button*,9>{&moveDatabase,&openDatabase,&relocateDatabase,&backupDatabase,
                                                    &audio,&reportBug,&reportFeature,&reportFormat,&close})
    {
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
    }
    moveDatabase.setEnabled(! indexing); openDatabase.setEnabled(! indexing);
    relocateDatabase.setEnabled(! indexing);
    moveDatabase.setTooltip("Copy the current catalog through SQLite and switch to the copy");
    openDatabase.setTooltip("Open an existing SONICATOR catalog without moving the current one");
    relocateDatabase.setTooltip("Rewrite catalog paths for folders that moved to this computer");
    backupDatabase.setTooltip("Save a consistent copy of the catalog database; original audio files are not included");
    close.setVisible(false); // The title bar is the single close control.
    close.setTooltip("Close");
    setDatabaseFile(database);
    setDatabaseStatus(indexing ? "Unavailable while indexing." : "Original audio files are not included in the database.",false);

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
        if (! onBackupRequested) return;
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
    setSize(560,866);
}
void SettingsPanel::setPluginMode(bool shouldUsePluginMode)
{
    pluginMode = shouldUsePluginMode;
    const std::array<juce::Component*,14> hidden {
        &scaleLabel, &scaleBox, &libraryHeading, &databaseKey, &databaseLabel, &libraryNote,
        &databaseStatus, &moveDatabase, &openDatabase, &relocateDatabase, &backupDatabase,
        &audioHeading, &audio, &contributeDataToggle };
    for (auto* c : hidden)
        c->setVisible(! pluginMode);
    close.setVisible(pluginMode);
    setSize(560, pluginMode ? 700 : 866);
    resized();
}
void SettingsPanel::notifySettings()
{
    if (onSettingsChanged) onSettingsChanged(current);
    sendLookAndFeelChange();
    repaint();
}
void SettingsPanel::chooseDatabase(bool moveCurrent)
{
    if (indexing) { setDatabaseStatus("Stop indexing before switching catalogs.",true); return; }
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
    databaseLabel.setText(database.getFullPathName(),juce::dontSendNotification);
    databaseLabel.setTooltip(database.getFullPathName());
}
void SettingsPanel::setDatabaseStatus(const juce::String& text, bool error)
{
    statusError = error;
    databaseStatus.setColour(juce::Label::textColourId,error
        ? (theme::isDark ? juce::Colour(0xffff8b8b) : juce::Colour(0xffb3261e)) : theme::muted);
    databaseStatus.setText(text,juce::dontSendNotification);
}
void SettingsPanel::lookAndFeelChanged()
{
    appearance.setColour(juce::Label::textColourId,theme::muted);
    languageHeading.setColour(juce::Label::textColourId,theme::muted);
    libraryHeading.setColour(juce::Label::textColourId,theme::muted);
    audioHeading.setColour(juce::Label::textColourId,theme::muted);
    aboutHeading.setColour(juce::Label::textColourId,theme::muted);
    themeLabel.setColour(juce::Label::textColourId,theme::text);
    scaleLabel.setColour(juce::Label::textColourId,theme::text);
    languageLabel.setColour(juce::Label::textColourId,theme::text);
    databaseKey.setColour(juce::Label::textColourId,theme::text);
    languageNote.setColour(juce::Label::textColourId,theme::muted);
    libraryNote.setColour(juce::Label::textColourId,theme::muted);
    databaseStatus.setColour(juce::Label::textColourId,statusError
        ? (theme::isDark ? juce::Colour(0xffff8b8b) : juce::Colour(0xffb3261e)) : theme::muted);
    contributeDataToggle.setColour(juce::ToggleButton::textColourId, theme::text);
    contributeDataToggle.setColour(juce::ToggleButton::tickColourId, theme::accent);
    authorLabel.setColour(juce::Label::textColourId,theme::muted);
    productLabel.setColour(juce::Label::textColourId,theme::text);
    title.setColour(juce::Label::textColourId,theme::text);
    reportHeading.setColour(juce::Label::textColourId,theme::muted);
    reportNote.setColour(juce::Label::textColourId,theme::muted);
    supportLabel.setColour(juce::Label::textColourId,theme::muted);
    creditsLabel.setColour(juce::Label::textColourId,theme::muted);
    for (auto* link : std::array<juce::HyperlinkButton*,5>{&gumroad,&bandcamp,&naukograd,&kofi,&boosty})
        link->setColour(juce::HyperlinkButton::textColourId,theme::accent);
    repaint();
}
void SettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    if (! databaseFieldBounds.isEmpty())
    {
        const auto field = databaseFieldBounds.toFloat().reduced(0.5f);
        g.setColour(theme::background);
        g.fillRoundedRectangle(field, 7.0f);
        g.setColour(theme::line);
        g.drawRoundedRectangle(field, 7.0f, 1.0f);
    }
    g.setColour(theme::line);
    for (const auto& rule : sectionRules)
        g.fillRect(rule);
}
void SettingsPanel::resized()
{
    sectionRules.clear();
    auto bounds = getLocalBounds().reduced(24,16);

    struct LayoutRow
    {
        int height = 0, gap = 0;
        bool stretch = false;
        std::function<void(juce::Rectangle<int>)> place;
    };
    std::vector<LayoutRow> rows;
    constexpr int labelColumn = 140;

    auto add = [&rows](int height, int gap, bool stretch, std::function<void(juce::Rectangle<int>)> place)
    {
        rows.push_back({ height, gap, stretch, std::move(place) });
    };
    auto section = [this, &add](juce::Label& label, int gapBefore)
    {
        add(14, gapBefore, true, [this, &label](juce::Rectangle<int> area)
        {
            label.setBounds(area);
            const auto textWidth = juce::GlyphArrangement::getStringWidthInt(label.getFont(), label.getText());
            const auto lineX = area.getX() + textWidth + 12;
            if (lineX < area.getRight())
                sectionRules.emplace_back((float) lineX, (float) area.getCentreY(),
                                          (float) (area.getRight() - lineX), 1.0f);
        });
    };
    auto row = [&add, labelColumn](juce::Label& label, juce::Component& control, int gapBefore)
    {
        add(28, gapBefore, false, [&label, &control, labelColumn](juce::Rectangle<int> area)
        {
            label.setBounds(area.removeFromLeft(labelColumn));
            control.setBounds(area);
        });
    };

    add(24, 0, false, [this](juce::Rectangle<int> area) { title.setBounds(area); });
    section(appearance, 12);
    row(themeLabel, themeBox, 4);
    if (scaleLabel.isVisible())
        row(scaleLabel, scaleBox, 4);
    section(languageHeading, 14);
    row(languageLabel, languageBox, 4);
    add(30, 6, false, [this](juce::Rectangle<int> area) { languageNote.setBounds(area); });

    if (libraryHeading.isVisible())
    {
        section(libraryHeading, 14);
        add(30, 4, false, [this](juce::Rectangle<int> area)
        {
            databaseKey.setBounds(area.removeFromLeft(140));
            databaseFieldBounds = area;
            databaseLabel.setBounds(area.reduced(0, 4));
        });
        add(30, 6, false, [this](juce::Rectangle<int> area)
        {
            auto right = area.withTrimmedLeft(140);
            const auto width = (right.getWidth() - 16) / 3;
            moveDatabase.setBounds(right.removeFromLeft(width));
            right.removeFromLeft(8);
            openDatabase.setBounds(right.removeFromLeft(width));
            right.removeFromLeft(8);
            relocateDatabase.setBounds(right);
        });
        add(30, 4, false, [this](juce::Rectangle<int> area)
        {
            backupDatabase.setBounds(area.withTrimmedLeft(140).removeFromLeft(180));
        });
        add(32, 6, false, [this](juce::Rectangle<int> area) { libraryNote.setBounds(area.withTrimmedLeft(140)); });
        add(24, 4, false, [this](juce::Rectangle<int> area) { databaseStatus.setBounds(area.withTrimmedLeft(140)); });
        add(26, 4, false, [this](juce::Rectangle<int> area) { contributeDataToggle.setBounds(area.withTrimmedLeft(140)); });
    }

    if (audioHeading.isVisible())
    {
        section(audioHeading, 14);
        add(30, 4, false, [this](juce::Rectangle<int> area) { audio.setBounds(area.withTrimmedLeft(140)); });
    }

    section(reportHeading, 14);
    add(28, 4, false, [this](juce::Rectangle<int> area)
    {
        auto right = area.withTrimmedLeft(140);
        const auto width = (right.getWidth() - 16) / 3;
        reportBug.setBounds(right.removeFromLeft(width));
        right.removeFromLeft(8);
        reportFeature.setBounds(right.removeFromLeft(width));
        right.removeFromLeft(8);
        reportFormat.setBounds(right);
    });
    add(40, 6, false, [this](juce::Rectangle<int> area) { reportNote.setBounds(area.withTrimmedLeft(140)); });

    section(aboutHeading, 14);
    add(24, 2, false, [this](juce::Rectangle<int> area) { productLabel.setBounds(area); });
    add(18, 0, false, [this](juce::Rectangle<int> area) { authorLabel.setBounds(area); });
    add(60, 2, false, [this](juce::Rectangle<int> area)
    {
        auto r = area;
        for (auto* link : { &gumroad, &bandcamp, &naukograd })
            link->setBounds(r.removeFromTop(20));
    });
    add(24, 6, false, [this](juce::Rectangle<int> area) { supportLabel.setBounds(area); });
    add(20, 2, false, [this](juce::Rectangle<int> area)
    {
        auto left = area.removeFromLeft(area.getWidth() / 2);
        kofi.setBounds(left);
        boosty.setBounds(area);
    });
    add(58, 10, false, [this](juce::Rectangle<int> area) { creditsLabel.setBounds(area); });
    if (close.isVisible())
        add(30, 12, false, [this](juce::Rectangle<int> area) { close.setBounds(area.removeFromRight(110)); });

    int fixedHeight = 0, stretchCount = 0;
    for (const auto& r : rows)
    {
        fixedHeight += r.height + r.gap;
        if (r.stretch) ++stretchCount;
    }
    const int extra = stretchCount > 0 ? juce::jmax(0, (bounds.getHeight() - fixedHeight) / stretchCount) : 0;

    int y = bounds.getY();
    for (const auto& r : rows)
    {
        y += r.gap + (r.stretch ? extra : 0);
        r.place({ bounds.getX(), y, bounds.getWidth(), r.height });
        y += r.height;
    }

}
} // namespace sonicator
