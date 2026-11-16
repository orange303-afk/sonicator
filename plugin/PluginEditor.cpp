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
    // Read at first editor creation rather than processor construction: a host may construct
    // several processors before showing any of them, and the last user resize should still win.
    AppSettings initial = SettingsStore(processor.settingsFile()).load();
    processor.initialiseWindowSize(initial.pluginWindowWidth > 0 && initial.pluginWindowHeight > 0
                                      ? initial.pluginWindowWidth : PluginState::defaultWidth,
                                  initial.pluginWindowWidth > 0 && initial.pluginWindowHeight > 0
                                      ? initial.pluginWindowHeight : PluginState::defaultHeight);
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
        // The match intent belongs to this project and the host tempo to this instance; the
        // window only turns a selected catalog file into effective metadata.
        main->setPluginMatchContext(
            [this] { return processor.pluginState().match; },
            [this] (const PluginMatchPreferences& preferences)
            {
                processor.setMatchPreferences (preferences);
            },
            [this] { return processor.hostTempo(); });
        main->applyPluginMatchPreferences (saved.match);
        if (main->catalogIsOpen())
        {
            main->onInterfaceScaleChanged = [this](float scale) { applyInterfaceScale(scale); };
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

    // The host owns window resizing; omit JUCE's diagonal bottom-right grip.
    setResizable(true, false);
    applyInterfaceScale(main != nullptr ? main->interfaceScale() : 1.0f);
    setSize(juce::roundToInt(editorScale * juce::jlimit(PluginState::minimumWidth,
                                                     PluginState::maximumWidth, saved.windowWidth)),
            juce::roundToInt(editorScale * juce::jlimit(PluginState::minimumHeight,
                                                     PluginState::maximumHeight, saved.windowHeight)));
    rememberedWidth = juce::roundToInt(getWidth() / editorScale);
    rememberedHeight = juce::roundToInt(getHeight() / editorScale);
    initialising = false;
    // The host can load another project into this running instance. Polling the processor's
    // generation is how the window learns about it: the message thread is the only one allowed to
    // touch these components, and the processor is called from the host's thread.
    startTimerHz(10);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    if (processor.stateGeneration() == stateGenerationAtOpen) persistWindowSize();
    if (main != nullptr)
    {
        // The project keeps the final selection even when the host never asked for state again,
        // unless the host restored a newer state while this window was open.
        if (processor.stateGeneration() == stateGenerationAtOpen)
        {
            processor.updateSession(main->selectedFolderPath(), main->selectedSamplePath());
            processor.updateWindowSize(juce::roundToInt(getWidth() / editorScale),
                                       juce::roundToInt(getHeight() / editorScale));
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
    if (current == stateGenerationAtOpen)
    {
        if (windowSizeDirty && juce::Time::getMillisecondCounter() - lastWindowResize >= 250)
            persistWindowSize();
        return;
    }
    // Restoring a saved project is not a user resize and must not replace the default size
    // for the next fresh instance (nor publish a resize that raced the restored state).
    const juce::ScopedValueSetter<bool> restoring(restoringProjectSize, true);
    windowSizeDirty = false;
    stateGenerationAtOpen = current;
    const auto saved = processor.pluginState();
    if (main != nullptr)
    {
        main->applySessionSelection(saved.lastFolder, saved.lastSamplePath);
        main->applyPluginMatchPreferences(saved.match);
    }
    const int width = juce::jlimit(PluginState::minimumWidth, PluginState::maximumWidth,
                                   saved.windowWidth);
    const int height = juce::jlimit(PluginState::minimumHeight, PluginState::maximumHeight,
                                    saved.windowHeight);
    const int scaledWidth = juce::roundToInt(width * editorScale);
    const int scaledHeight = juce::roundToInt(height * editorScale);
    if (scaledWidth != getWidth() || scaledHeight != getHeight()) setSize(scaledWidth, scaledHeight);
    rememberedWidth = width;
    rememberedHeight = height;
}

void PluginEditor::persistWindowSize()
{
    if (! windowSizeDirty || main == nullptr) return;
    const int width = juce::roundToInt(getWidth() / editorScale);
    const int height = juce::roundToInt(getHeight() / editorScale);
    try
    {
        SettingsStore(processor.settingsFile()).update([width, height](AppSettings& settings)
        {
            settings.pluginWindowWidth = width;
            settings.pluginWindowHeight = height;
        });
        rememberedWidth = width;
        rememberedHeight = height;
        windowSizeDirty = false;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Plugin window size was not saved: " + juce::String::fromUTF8(e.what()));
    }
}

void PluginEditor::applyInterfaceScale(float scale)
{
    const int width = juce::roundToInt(getWidth() / editorScale);
    const int height = juce::roundToInt(getHeight() / editorScale);
    editorScale = scale;
    setResizeLimits(juce::roundToInt(PluginState::minimumWidth * scale),
                    juce::roundToInt(PluginState::minimumHeight * scale),
                    juce::roundToInt(PluginState::maximumWidth * scale),
                    juce::roundToInt(PluginState::maximumHeight * scale));
    setSize(juce::roundToInt(juce::jlimit(PluginState::minimumWidth, PluginState::maximumWidth, width) * scale),
            juce::roundToInt(juce::jlimit(PluginState::minimumHeight, PluginState::maximumHeight, height) * scale));
    resized();
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
        const auto scale = main->interfaceScale();
        main->setTransform(juce::AffineTransform::scale(scale));
        main->setBounds(0, 0, juce::roundToInt(bounds.getWidth() / scale),
                             juce::roundToInt(bounds.getHeight() / scale));
        processor.updateWindowSize(juce::roundToInt(getWidth() / scale),
                                   juce::roundToInt(getHeight() / scale));
        if (! initialising && ! restoringProjectSize
            && processor.stateGeneration() == stateGenerationAtOpen)
        {
            windowSizeDirty = juce::roundToInt(getWidth() / scale) != rememberedWidth
                || juce::roundToInt(getHeight() / scale) != rememberedHeight;
            lastWindowResize = juce::Time::getMillisecondCounter();
        }
    }
    else
    {
        unavailable.setBounds(bounds);
    }
}
} // namespace sonicator::plugin
