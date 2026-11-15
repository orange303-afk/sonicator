#include "plugin/PluginEditor.h"
#include "ui/MainComponent.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"

namespace sonicator::plugin
{
PluginEditor::PluginEditor(PluginProcessor& owner)
    : juce::AudioProcessorEditor(owner), processor(owner)
{
    const auto saved = processor.pluginState();
    const auto database = processor.databaseFile();
    const auto settingsFile = processor.settingsFile();

    if (database.existsAsFile())
    {
        // The plugin follows the application's theme and editing preferences but never its
        // session: the last folder and sample belong to the DAW project.
        AppSettings initial = SettingsStore(settingsFile).load();
        initial.midiPreview = true; // a DAW keyboard plays the selection without a toggle
        initial.lastFolder.clear();
        initial.lastSamplePath.clear();

        EmbeddedSession session;
        session.lastFolder = saved.lastFolder;
        session.lastSamplePath = saved.lastSamplePath;
        session.onChanged = [this](const juce::String& folder, const juce::String& sample)
        {
            processor.updateSession(folder, sample);
        };
        main = std::make_unique<MainComponent>(database, false, initial, settingsFile,
                                               &processor.engine(), true, std::move(session));
        main->setComponentID("pluginMain");
        addAndMakeVisible(*main);
    }
    else
    {
        unavailable.setText("SONICATOR reads the library the application owns.\n\n"
                            "Open the SONICATOR app once so it creates its catalog, then reopen this"
                            " window.\n\n" + database.getFullPathName(),
                            juce::dontSendNotification);
        unavailable.setJustificationType(juce::Justification::centred);
        unavailable.setColour(juce::Label::textColourId, theme::muted);
        unavailable.setFont(fonts::font(13));
        unavailable.setComponentID("pluginMissingCatalog");
        addAndMakeVisible(unavailable);
    }

    setResizable(true, true);
    setResizeLimits(PluginState::minimumWidth, PluginState::minimumHeight,
                    PluginState::maximumWidth, PluginState::maximumHeight);
    setSize(juce::jlimit(PluginState::minimumWidth, PluginState::maximumWidth, saved.windowWidth),
            juce::jlimit(PluginState::minimumHeight, PluginState::maximumHeight, saved.windowHeight));
}

PluginEditor::~PluginEditor()
{
    if (main != nullptr)
    {
        // The project keeps the final selection even when the host never asked for state again.
        processor.updateSession(main->selectedFolderPath(), main->selectedSamplePath());
        processor.updateWindowSize(getWidth(), getHeight());
        main.reset();
    }
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
}

void PluginEditor::resized()
{
    auto bounds = getLocalBounds();
    if (main != nullptr)
    {
        main->setBounds(bounds);
        processor.updateWindowSize(getWidth(), getHeight());
    }
    else
    {
        unavailable.setBounds(bounds);
    }
}
} // namespace sonicator::plugin
