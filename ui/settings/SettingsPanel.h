#pragma once
#include "core/settings/AppSettings.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel(const AppSettings&, juce::File databaseFile, bool indexing);
    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void setDatabaseFile(juce::File);
    void setDatabaseStatus(const juce::String&, bool error);
    std::function<void(const AppSettings&)> onSettingsChanged;
    std::function<void(const juce::File&, bool moveCurrent)> onDatabaseChosen;
    std::function<void()> onShowAudio;
private:
    AppSettings current;
    juce::File database;
    bool indexing = false, statusError = false;
    juce::Label title, appearance, themeLabel, scaleLabel, languageHeading, languageLabel,
                 languageNote, libraryHeading, databaseLabel, libraryNote, databaseStatus,
                 audioHeading, aboutHeading, productLabel, authorLabel, supportLabel;
    juce::ComboBox themeBox, scaleBox, languageBox;
    juce::TextButton moveDatabase { "Move catalog..." }, openDatabase { "Open catalog..." },
                     audio { "Audio output..." }, close { "Close" };
    juce::HyperlinkButton gumroad, bandcamp, naukograd, kofi;
    std::unique_ptr<juce::FileChooser> chooser;
    void notifySettings();
    void chooseDatabase(bool moveCurrent);
};
} // namespace sonicator
