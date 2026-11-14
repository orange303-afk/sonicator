#pragma once
#include "SettingsSection.h"
#include "core/settings/AppSettings.h"
#include "core/settings/ShortcutPreferences.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>

namespace sonicator {
class CommandRegistry;
class HotkeysPanel;

// Settings with a fixed navigation column and one page at a time. Every page keeps its own
// scroll position; the panel never rebuilds a page when another one is selected. Callbacks and
// controls keep their previous meaning, so the owner still drives catalog, consent and backup.
class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel(const AppSettings&, juce::File databaseFile, bool indexing);
    ~SettingsPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress&) override;
    void setDatabaseFile(juce::File);
    void setDatabaseStatus(const juce::String&, bool error);
    // Plugin mode: the host owns audio and the catalog, so those sections are hidden and the
    // panel gets its own Close button instead of relying on a dialog title bar.
    void setPluginMode(bool pluginMode);
    void setBusyState(bool catalogBusy);
    void selectSection(SettingsSection);
    SettingsSection selectedSection() const { return section; }
    // The Hotkeys page reads this catalogue; the panel only edits and returns a candidate profile.
    void setCommandRegistry(const CommandRegistry*);
    void setShortcutDiagnostics(const juce::StringArray&);
    void setShortcutProfile(const ShortcutProfile&);
    void setAudioSettingsFactory(std::function<std::unique_ptr<juce::Component>()>);

    std::function<void()> onClose;
    std::function<void(const AppSettings&)> onSettingsChanged;
    std::function<void(const juce::File&, bool moveCurrent)> onDatabaseChosen;
    std::function<void()> onRelocateRequested;
    // Kept for callers that want the standalone Audio/MIDI window; the Audio & MIDI page uses the
    // factory instead, and this is only reached if no factory was supplied.
    std::function<void()> onShowAudio;
    // Writes a consistent catalog copy and returns it; on failure returns an invalid file and
    // fills error.
    std::function<juce::File(juce::String& error)> onBackupRequested;
    // Training-data contribution. The panel never persists anything itself: the owner decides
    // whether consent is current, runs the request and writes the setting.
    std::function<void(bool)> onContributionToggle;
    std::function<juce::String()> onContributionPreview;   // returns the next batch as JSON
    std::function<void(std::function<void(bool, juce::String)>)> onContributionDelete;
    // Returns false when the profile could not be saved; the Hotkeys page then keeps the entry.
    std::function<bool(const ShortcutProfile&)> onShortcutProfileChanged;
    std::function<void(SettingsSection)> onSectionChanged;
    std::function<void()> onResetLayout;
    void setContributionEnabled(bool);

private:
    void notifySettings();
    void showContributionPreview();
    void confirmContributionDelete();
    void chooseDatabase(bool moveCurrent);
    void updateSectionVisibility();
    void layoutGeneralPage(juce::Rectangle<int>);
    void layoutAboutPage(juce::Rectangle<int>);
    void ensureAudioPage();
    bool sectionAvailable(SettingsSection) const;
    int pageHeight(bool general) const;

    AppSettings current;
    juce::File database;
    bool indexing = false, statusError = false, pluginMode = false, catalogBusy = false;
    SettingsSection section = SettingsSection::general;
    const CommandRegistry* registry = nullptr;
    ShortcutProfile profile;
    std::function<std::unique_ptr<juce::Component>()> audioFactory;
    bool audioPageBuilt = false;

    juce::Label title, appearance, themeLabel, scaleLabel, languageHeading, languageLabel,
                 languageNote, libraryHeading, databaseKey, databaseLabel, libraryNote, databaseStatus,
                 editingHeading, alwaysOverwriteNote, workspaceHeading, workspaceNote,
                 audioHeading, audioNote, reportHeading, reportNote, aboutHeading, productLabel,
                 authorLabel, supportLabel, creditsLabel, prototypesLabel,
                 pluginsHeading, pluginsNote, privacyHeading;
    juce::ComboBox themeBox, scaleBox, languageBox;
    juce::ToggleButton contributeDataToggle;
    juce::ToggleButton alwaysOverwriteToggle { "Always overwrite original files" };
    juce::Label contributionNote;
    juce::TextButton contributionPreview { "What will be sent..." },
                     contributionDelete { "Delete my contributed data..." };
    juce::TextButton moveDatabase { "Move catalog..." }, openDatabase { "Open catalog..." },
                     relocateDatabase { "Relocate folders..." }, backupDatabase { "Backup database" },
                     audio { "Audio/MIDI Settings" }, resetLayout { "Reset panel layout" },
                     reportBug { "Report a bug..." }, reportFeature { "Request a feature..." },
                     reportFormat { "Format support..." },
                     reportDiscuss { "Ask or discuss..." },
                     close { "Close" };
    juce::HyperlinkButton gumroad, bandcamp, naukograd, kofi, boosty;
    std::unique_ptr<juce::FileChooser> chooser;

    // Pages last, so they are destroyed before the controls they host.
    std::array<juce::TextButton, 5> nav;
    juce::Viewport generalViewport, aboutViewport;
    juce::Component generalContent, aboutContent;
    juce::Component audioContent;
    std::unique_ptr<HotkeysPanel> hotkeys;
    juce::Component pluginsContent;
    juce::Component* audioPage = nullptr;
    std::unique_ptr<juce::Component> audioOwned;

    std::vector<juce::Rectangle<float>> sectionRules;
    std::vector<juce::Rectangle<float>> aboutRules;
    juce::Rectangle<int> databaseFieldBounds;
};
} // namespace sonicator
