#include "plugin/PluginEditor.h"
#include "core/database/CatalogCompatibility.h"
#include "ui/MainComponent.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"

namespace sonicator::plugin
{
PluginEditor::PluginEditor(PluginProcessor& owner)
    : juce::AudioProcessorEditor(owner), processor(owner)
{
    const auto saved = processor.pluginState();
    stateGenerationAtOpen = processor.stateGeneration();
    const auto database = processor.databaseFile();
    const auto settingsFile = processor.settingsFile();

    // The catalog belongs to the application. A plug-in looks at its schema version first and
    // never migrates it: a library written by another build stays readable to that build.
    const auto compatibility = inspectCatalog(database);
    if (compatibility.readable())
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
        if (main->catalogIsOpen())
        {
            addAndMakeVisible(*main);
        }
        else
        {
            // A catalog that passed the version check can still fail to open (a lock, a corrupt
            // page). The window then explains it and keeps the project's selection untouched.
            const auto error = main->catalogError();
            main.reset();
            showCatalogProblem(error, "pluginCatalogBlocked");
        }
    }
    else
    {
        const auto state = compatibility.state == CatalogCompatibility::State::missing
                               ? "pluginMissingCatalog" : "pluginCatalogBlocked";
        showCatalogProblem(compatibility.message, state);
    }

    setResizable(true, true);
    setResizeLimits(PluginState::minimumWidth, PluginState::minimumHeight,
                    PluginState::maximumWidth, PluginState::maximumHeight);
    setSize(juce::jlimit(PluginState::minimumWidth, PluginState::maximumWidth, saved.windowWidth),
            juce::jlimit(PluginState::minimumHeight, PluginState::maximumHeight, saved.windowHeight));
    // The host can load another project into this running instance. Polling the processor's
    // generation is how the window learns about it: the message thread is the only one allowed to
    // touch these components, and the processor is called from the host's thread.
    startTimerHz(10);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    if (main != nullptr)
    {
        // The project keeps the final selection even when the host never asked for state again,
        // unless the host restored a newer state while this window was open.
        if (processor.stateGeneration() == stateGenerationAtOpen)
        {
            processor.updateSession(main->selectedFolderPath(), main->selectedSamplePath());
            processor.updateWindowSize(getWidth(), getHeight());
        }
        main.reset();
    }
}

void PluginEditor::showCatalogProblem(const juce::String& message, const juce::String& componentId)
{
    unavailable.setText(message + "\n\n" + processor.databaseFile().getFullPathName(),
                        juce::dontSendNotification);
    unavailable.setJustificationType(juce::Justification::centred);
    unavailable.setColour(juce::Label::textColourId, theme::muted);
    unavailable.setFont(fonts::font(13));
    unavailable.setComponentID(componentId);
    addAndMakeVisible(unavailable);
}

void PluginEditor::timerCallback()
{
    const int current = processor.stateGeneration();
    if (current == stateGenerationAtOpen) return;
    stateGenerationAtOpen = current;
    const auto saved = processor.pluginState();
    if (main != nullptr)
        main->applySessionSelection(saved.lastFolder, saved.lastSamplePath);
    const int width = juce::jlimit(PluginState::minimumWidth, PluginState::maximumWidth,
                                   saved.windowWidth);
    const int height = juce::jlimit(PluginState::minimumHeight, PluginState::maximumHeight,
                                    saved.windowHeight);
    if (width != getWidth() || height != getHeight()) setSize(width, height);
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
