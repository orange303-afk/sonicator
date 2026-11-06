#include "SettingsPanel.h"
#include "ui/shell/Theme.h"
namespace sonicator {
SettingsPanel::SettingsPanel(const AppSettings& initial, juce::File file, bool busy)
    : current(initial), database(std::move(file)), indexing(busy),
      gumroad("ilyaorange.gumroad.com", juce::URL("https://ilyaorange.gumroad.com")),
      bandcamp("ilyaorange.bandcamp.com", juce::URL("https://ilyaorange.bandcamp.com")),
      naukograd("naukograd.bandcamp.com", juce::URL("https://naukograd.bandcamp.com")),
      kofi("ko-fi.com/ilyaorange", juce::URL("https://ko-fi.com/ilyaorange"))
{
    for (auto* c : std::array<juce::Component*,20>{&title,&appearance,&themeLabel,&scaleLabel,&languageHeading,
         &languageLabel,&languageNote,&libraryHeading,&databaseLabel,&libraryNote,&databaseStatus,&audioHeading,
         &aboutHeading,&productLabel,&authorLabel,&supportLabel,&themeBox,&scaleBox,&languageBox,&moveDatabase})
        addAndMakeVisible(c);
    for (auto* c : std::array<juce::Component*,7>{&openDatabase,&audio,&close,&gumroad,&bandcamp,&naukograd,&kofi})
        addAndMakeVisible(c);

    title.setText("Settings",juce::dontSendNotification);
    title.setFont(juce::FontOptions(20,juce::Font::bold));
    appearance.setText("APPEARANCE",juce::dontSendNotification); appearance.setFont(juce::FontOptions(10));
    themeLabel.setText("Theme",juce::dontSendNotification); themeLabel.setFont(juce::FontOptions(13));
    scaleLabel.setText("Interface scale",juce::dontSendNotification); scaleLabel.setFont(juce::FontOptions(13));
    themeBox.setComponentID("settingsTheme");
    scaleBox.setComponentID("settingsScale");
    languageBox.setComponentID("settingsLanguage");
    themeBox.addItem("System",1); themeBox.addItem("Dark",2); themeBox.addItem("Light",3);
    themeBox.setSelectedId(initial.theme == ThemeMode::Dark ? 2 : (initial.theme == ThemeMode::Light ? 3 : 1),juce::dontSendNotification);
    scaleBox.addItem("100%",100); scaleBox.addItem("110%",110); scaleBox.addItem("125%",125); scaleBox.addItem("150%",150);
    scaleBox.setSelectedId(AppSettings::clampScale(initial.scalePercent),juce::dontSendNotification);
    languageHeading.setText("LANGUAGE",juce::dontSendNotification); languageHeading.setFont(juce::FontOptions(10));
    languageLabel.setText("Interface language",juce::dontSendNotification); languageLabel.setFont(juce::FontOptions(13));
    languageNote.setText("English is the only language in this stage. The choice is stored so later translations can use it.",juce::dontSendNotification);
    languageNote.setFont(juce::FontOptions(10)); languageNote.setJustificationType(juce::Justification::topLeft);
    languageBox.addItem("English",1); languageBox.setSelectedId(1,juce::dontSendNotification);
    languageBox.setEnabled(false);
    libraryHeading.setText("LIBRARY",juce::dontSendNotification); libraryHeading.setFont(juce::FontOptions(10));
    databaseLabel.setFont(juce::FontOptions(12)); databaseLabel.setJustificationType(juce::Justification::topLeft);
    libraryNote.setText(indexing
        ? "Indexing is running. Stop it before moving or opening another catalog."
        : "Move copies a consistent snapshot to a new location and switches to it; the old file is kept. Open uses an existing catalog.",juce::dontSendNotification);
    libraryNote.setFont(juce::FontOptions(10)); libraryNote.setJustificationType(juce::Justification::topLeft);
    databaseStatus.setFont(juce::FontOptions(11)); databaseStatus.setJustificationType(juce::Justification::topLeft);
    audioHeading.setText("AUDIO",juce::dontSendNotification); audioHeading.setFont(juce::FontOptions(10));
    aboutHeading.setText("ABOUT",juce::dontSendNotification); aboutHeading.setFont(juce::FontOptions(10));
    productLabel.setText("Sonicator",juce::dontSendNotification);
    productLabel.setFont(juce::FontOptions(18,juce::Font::bold));
    authorLabel.setText("by Ilya Orange",juce::dontSendNotification); authorLabel.setFont(juce::FontOptions(13));
    supportLabel.setText("Sonicator is free, but you can support further development:",juce::dontSendNotification);
    supportLabel.setFont(juce::FontOptions(11)); supportLabel.setJustificationType(juce::Justification::topLeft);
    for (auto* link : std::array<juce::HyperlinkButton*,4>{&gumroad,&bandcamp,&naukograd,&kofi})
    {
        link->setFont(juce::FontOptions(12),false);
        link->setJustificationType(juce::Justification::centredLeft);
    }
    for (auto* button : std::array<juce::Button*,4>{&moveDatabase,&openDatabase,&audio,&close})
    {
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
    }
    moveDatabase.setEnabled(! indexing); openDatabase.setEnabled(! indexing);
    moveDatabase.setTooltip("Copy the current catalog through SQLite and switch to the copy");
    openDatabase.setTooltip("Open an existing SONICATOR catalog without moving the current one");
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
    databaseLabel.setText("Database\n" + database.getFullPathName(),juce::dontSendNotification);
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
void SettingsPanel::paint(juce::Graphics& g) { g.fillAll(theme::panel); }
void SettingsPanel::resized()
{
    auto bounds = getLocalBounds().reduced(20,16);
    auto footer = bounds.removeFromBottom(34);
    auto r = bounds;
    title.setBounds(r.removeFromTop(26)); r.removeFromTop(6);
    auto section = [&](juce::Label& label)
    {
        label.setBounds(r.removeFromTop(16)); r.removeFromTop(4);
    };
    auto row = [&](juce::Label& label, juce::Component& control)
    {
        auto line = r.removeFromTop(28);
        label.setBounds(line.removeFromLeft(130));
        control.setBounds(line);
        r.removeFromTop(4);
    };
    section(appearance);
    row(themeLabel,themeBox);
    row(scaleLabel,scaleBox);
    r.removeFromTop(8);
    section(languageHeading);
    row(languageLabel,languageBox);
    languageNote.setBounds(r.removeFromTop(28));
    r.removeFromTop(8);
    section(libraryHeading);
    databaseLabel.setBounds(r.removeFromTop(38));
    auto buttons = r.removeFromTop(28);
    moveDatabase.setBounds(buttons.removeFromLeft(160)); buttons.removeFromLeft(8);
    openDatabase.setBounds(buttons.removeFromLeft(160));
    r.removeFromTop(6);
    libraryNote.setBounds(r.removeFromTop(30));
    databaseStatus.setBounds(r.removeFromTop(24));
    r.removeFromTop(6);
    section(audioHeading);
    audio.setBounds(r.removeFromTop(28));
    r.removeFromTop(8);
    section(aboutHeading);
    productLabel.setBounds(r.removeFromTop(24));
    authorLabel.setBounds(r.removeFromTop(18));
    gumroad.setBounds(r.removeFromTop(20));
    bandcamp.setBounds(r.removeFromTop(20));
    naukograd.setBounds(r.removeFromTop(20));
    r.removeFromTop(6);
    supportLabel.setBounds(r.removeFromTop(24));
    kofi.setBounds(r.removeFromTop(20));
    close.setBounds(footer.removeFromRight(92).withSizeKeepingCentre(92,28));
}
} // namespace sonicator
