#include "SettingsPanel.h"
#include "ui/shell/Theme.h"
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
      kofi("ko-fi.com/ilyaorange", juce::URL("https://ko-fi.com/ilyaorange"))
{
    for (auto* c : std::array<juce::Component*,22>{&title,&appearance,&themeLabel,&scaleLabel,&languageHeading,
         &languageLabel,&languageNote,&libraryHeading,&databaseKey,&databaseLabel,&libraryNote,&databaseStatus,
         &audioHeading,&aboutHeading,&productLabel,&authorLabel,&supportLabel,&themeBox,&scaleBox,&languageBox,&moveDatabase,&relocateDatabase})
        addAndMakeVisible(c);
    for (auto* c : std::array<juce::Component*,7>{&openDatabase,&audio,&close,&gumroad,&bandcamp,&naukograd,&kofi})
        addAndMakeVisible(c);

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
    for (auto* link : std::array<juce::HyperlinkButton*,4>{&gumroad,&bandcamp,&naukograd,&kofi})
    {
        link->setFont(juce::FontOptions(12),false);
        link->setJustificationType(juce::Justification::centredLeft);
    }
    for (auto* button : std::array<juce::Button*,5>{&moveDatabase,&openDatabase,&relocateDatabase,&audio,&close})
    {
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
    }
    moveDatabase.setEnabled(! indexing); openDatabase.setEnabled(! indexing);
    relocateDatabase.setEnabled(! indexing);
    moveDatabase.setTooltip("Copy the current catalog through SQLite and switch to the copy");
    openDatabase.setTooltip("Open an existing SONICATOR catalog without moving the current one");
    relocateDatabase.setTooltip("Rewrite catalog paths for folders that moved to this computer");
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
    audio.onClick = [this] { if (onShowAudio) onShowAudio(); };
    close.onClick = [this]
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) dialog->exitModalState(0);
    };
    setSize(560,700);
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
    authorLabel.setColour(juce::Label::textColourId,theme::muted);
    supportLabel.setColour(juce::Label::textColourId,theme::muted);
    for (auto* link : std::array<juce::HyperlinkButton*,4>{&gumroad,&bandcamp,&naukograd,&kofi})
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
    auto footer = bounds.removeFromBottom(34);
    bounds.removeFromBottom(10); // keep the last row clear of the footer

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
    row(scaleLabel, scaleBox, 4);
    section(languageHeading, 14);
    row(languageLabel, languageBox, 4);
    add(30, 6, false, [this](juce::Rectangle<int> area) { languageNote.setBounds(area); });

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
    add(32, 6, false, [this](juce::Rectangle<int> area) { libraryNote.setBounds(area.withTrimmedLeft(140)); });
    add(24, 4, false, [this](juce::Rectangle<int> area) { databaseStatus.setBounds(area.withTrimmedLeft(140)); });

    section(audioHeading, 14);
    add(30, 4, false, [this](juce::Rectangle<int> area) { audio.setBounds(area.withTrimmedLeft(140)); });

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
    add(20, 2, false, [this](juce::Rectangle<int> area) { kofi.setBounds(area); });

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

    close.setBounds(footer.removeFromRight(92).withSizeKeepingCentre(92,28));
}
} // namespace sonicator
