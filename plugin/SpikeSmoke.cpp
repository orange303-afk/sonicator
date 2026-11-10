// Opens the spike editor once, with the real catalog copy, and quits after a few seconds.
// Purpose: catch crashes or exceptions in MainComponent construction before the plugin is
// handed to a DAW host, record how long the editor takes to become ready, and optionally time
// library selections the way a user feels them.
// SONICATOR_SPIKE_SELECT=<rows> selects that many rows in turn and logs each duration.
#include "plugin/SpikeProcessor.h"
#include "plugin/SpikeEditor.h"
#include "ui/library/FileListPanel.h"
#include "ui/settings/SettingsPanel.h"
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdlib>

namespace {
juce::File spikeFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/plugin-spike");
}

class SmokeApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Sonicator Spike Smoke"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}
    void systemRequestedQuit() override { quit(); }
    void suspended() override {}
    void resumed() override {}
    void unhandledException(const std::exception*, const juce::String&, int) override {}

    void initialise(const juce::String&) override
    {
        processor = std::make_unique<sonicator::spike::SpikeProcessor>();
        editor.reset(processor->createEditor());
        if (editor != nullptr)
            editor->setVisible(true);
        startTimer(1000 + 3000);
    }

    void shutdown() override
    {
        editor.reset();
        processor.reset();
    }

    void timerCallback() override
    {
        if (selectionDone)
        {
            stopTimer();
            quit();
            return;
        }
        selectionDone = true;
        stopTimer();
        benchmarkSelections();
        checkEmbeddedSettings();
        quit();
    }

private:
    // Reproduces the plugin case where the settings dialog used to close on the first click:
    // the panel must live inside the editor, and changing the theme must keep it open.
    void checkEmbeddedSettings()
    {
        if (std::getenv("SONICATOR_SPIKE_SETTINGS") == nullptr || editor == nullptr)
            return;
        auto* main = editor->getNumChildComponents() > 0 ? editor->getChildComponent(0) : nullptr;
        const auto log = spikeFolder().getChildFile("settings.log");
        log.deleteFile();
        if (main == nullptr)
        {
            log.appendText("no MainComponent\n");
            return;
        }
        juce::Button* settingsButton = nullptr;
        for (int i = 0; i < main->getNumChildComponents() && settingsButton == nullptr; ++i)
            if (auto* button = dynamic_cast<juce::TextButton*>(main->getChildComponent(i)))
                if (button->getButtonText() == "Settings")
                    settingsButton = button;
        if (settingsButton == nullptr)
        {
            log.appendText("no Settings button\n");
            return;
        }
        settingsButton->onClick();
        auto* overlay = main->findChildWithID("settingsOverlay");
        auto* panel = overlay != nullptr
            ? dynamic_cast<sonicator::SettingsPanel*>(overlay->findChildWithID("settingsPanel")) : nullptr;
        log.appendText(juce::String("panel=") + (panel != nullptr ? "found" : "missing") + "\n");
        if (panel == nullptr)
            return;
        log.appendText(juce::String("topLevelIsEditor=")
            + (panel->getTopLevelComponent() == (juce::Component*) editor.get() ? "yes" : "no") + "\n");
        log.appendText(juce::String("visible=") + (panel->isVisible() ? "yes" : "no") + "\n");
        if (auto* theme = dynamic_cast<juce::ComboBox*>(panel->findChildWithID("settingsTheme")))
        {
            theme->setSelectedId(2, juce::sendNotificationSync);
            const auto stillThere = overlay->findChildWithID("settingsPanel") != nullptr;
            log.appendText(juce::String("themeApplied=") + (theme->getSelectedId() == 2 ? "yes" : "no")
                + " panelAfterTheme=" + (stillThere ? "open" : "closed") + "\n");
        }
        else
        {
            log.appendText("no theme box\n");
        }
        log.appendText("done\n");
    }

    void benchmarkSelections()
    {
        const auto requested = std::getenv("SONICATOR_SPIKE_SELECT");
        if (requested == nullptr || editor == nullptr)
            return;
        const auto count = juce::jlimit(0, 200, std::atoi(requested));
        // findChildWithID only looks at direct children: the editor holds MainComponent, which
        // holds the library panel.
        sonicator::FileListPanel* library = nullptr;
        for (int i = 0; i < editor->getNumChildComponents() && library == nullptr; ++i)
            if (auto* child = editor->getChildComponent(i))
                library = dynamic_cast<sonicator::FileListPanel*>(child->findChildWithID("library"));
        const auto log = spikeFolder().getChildFile("selection.log");
        log.deleteFile();
        if (library == nullptr)
        {
            log.appendText("library not found\n");
            return;
        }
        log.appendText("rows=" + juce::String(library->getNumRows()) + "\n");
        const auto rows = juce::jmin(count, library->getNumRows());
        for (int row = 0; row < rows; ++row)
        {
            const auto started = juce::Time::getMillisecondCounterHiRes();
            library->selectRow(row);
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - started;
            const auto selected = library->getSelectedSamples();
            const auto path = selected.empty() ? juce::String() : selected.front().filePath;
            log.appendText("row=" + juce::String(row) + " selectMs=" + juce::String(elapsed, 1)
                + " file=" + path + "\n");
        }
        log.appendText("done\n");
    }

    std::unique_ptr<sonicator::spike::SpikeProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool selectionDone = false;
};
} // namespace

START_JUCE_APPLICATION(SmokeApp)
