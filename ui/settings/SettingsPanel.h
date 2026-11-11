#pragma once
#include "core/settings/AppSettings.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
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
    // Plugin mode: the host owns audio and the catalog, so those sections are hidden and the
    // panel gets its own Close button instead of relying on a dialog title bar.
    void setPluginMode(bool pluginMode);
    std::function<void()> onClose;
    std::function<void(const AppSettings&)> onSettingsChanged;
    std::function<void(const juce::File&, bool moveCurrent)> onDatabaseChosen;
    std::function<void()> onRelocateRequested;
    std::function<void()> onShowAudio;
    // Writes a consistent catalog copy and returns it; on failure returns an invalid file and
    // fills error.
    std::function<juce::File(juce::String& error)> onBackupRequested;
    // Training-data contribution. The panel never persists anything itself: the owner decides
    // whether consent is current, runs the request and writes the setting.
    std::function<void(bool)> onContributionToggle;
    std::function<juce::String()> onContributionPreview;   // returns the next batch as JSON
    std::function<void(std::function<void(bool, juce::String)>)> onContributionDelete;
    void setContributionEnabled(bool);
private:
    AppSettings current;
    juce::File database;
    bool indexing = false, statusError = false, pluginMode = false;
    juce::Label title, appearance, themeLabel, scaleLabel, languageHeading, languageLabel,
                 languageNote, libraryHeading, databaseKey, databaseLabel, libraryNote, databaseStatus,
                 audioHeading, reportHeading, reportNote, aboutHeading, productLabel, authorLabel,
                 supportLabel, creditsLabel;
    juce::ComboBox themeBox, scaleBox, languageBox;
    juce::ToggleButton contributeDataToggle;
    juce::Label contributionNote;
    juce::TextButton contributionPreview { "What will be sent..." },
                     contributionDelete { "Delete my contributed data..." };
    juce::TextButton moveDatabase { "Move catalog..." }, openDatabase { "Open catalog..." },
                     relocateDatabase { "Relocate folders..." }, backupDatabase { "Backup database" },
                     audio { "Audio/MIDI Settings" },
                     reportBug { "Report a bug..." }, reportFeature { "Request a feature..." },
                     reportFormat { "Format support..." },
                     reportDiscuss { "Ask or discuss..." },
                     close { "Close" };
    juce::HyperlinkButton gumroad, bandcamp, naukograd, kofi, boosty;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::Rectangle<float>> sectionRules;
    juce::Rectangle<int> databaseFieldBounds;
    void notifySettings();
    void showContributionPreview();
    void confirmContributionDelete();
    void chooseDatabase(bool moveCurrent);
};
} // namespace sonicator
