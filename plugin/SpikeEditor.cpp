#include "plugin/SpikeEditor.h"
#include "ui/MainComponent.h"

namespace sonicator::spike {
namespace {
constexpr int defaultWidth = 1180;
constexpr int defaultHeight = 720;
constexpr int minimumWidth = 880;
constexpr int minimumHeight = 580;

juce::String wrapperName()
{
   #if defined(JucePlugin_Build_VST3)
    return "VST3";
   #elif defined(JucePlugin_Build_AU)
    return "AU";
   #else
    return "unknown";
   #endif
}
} // namespace

juce::File SpikeEditor::spikeFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator")
        .getChildFile("plugin-spike");
}

void SpikeEditor::logEvent(const juce::String& event)
{
    const auto line = juce::Time::getCurrentTime().toString(false, true, true, true) + " host="
        + juce::PluginHostType().getHostDescription() + " wrapper=" + wrapperName() + " " + event + "\n";
    spikeFolder().getChildFile("spike.log").appendText(line);
}

SpikeEditor::SpikeEditor(SpikeProcessor& processor)
    : juce::AudioProcessorEditor(processor)
{
    const auto folder = spikeFolder();
    const auto database = folder.getChildFile("sonicator.sqlite");
    const auto settings = folder.getChildFile("settings.json");
    logEvent("editor-open start");

    if (database.existsAsFile())
    {
        const auto started = juce::Time::getMillisecondCounterHiRes();
        sonicator::AppSettings initial;
        // A DAW keyboard plays the selected sample without the user enabling MIDI preview.
        initial.midiPreview = true;
        main = std::make_unique<sonicator::MainComponent>(
            database, false, initial, settings, &processor.engine(), true);
        addAndMakeVisible(*main);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - started;
        logEvent("editor-open ready db=" + database.getFullPathName()
            + " constructMs=" + juce::String(elapsed, 1));
    }
    else
    {
        unavailable.setText("Sonicator Spike needs a catalog copy at:\n" + database.getFullPathName(),
                            juce::dontSendNotification);
        unavailable.setJustificationType(juce::Justification::centred);
        unavailable.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(unavailable);
        logEvent("editor-open missing-database path=" + database.getFullPathName());
    }

    setResizable(true, true);
    setResizeLimits(minimumWidth, minimumHeight, 3840, 2160);
    setSize(defaultWidth, defaultHeight);
}

SpikeEditor::~SpikeEditor()
{
    logEvent("editor-close");
    // The explicit reset keeps the destruction order obvious: MainComponent, then the editor.
    main.reset();
    logEvent("editor-close done");
}

void SpikeEditor::paint(juce::Graphics& g)
{
    g.fillAll(sonicator::theme::background);
}

void SpikeEditor::resized()
{
    auto bounds = getLocalBounds();
    if (main != nullptr)
        main->setBounds(bounds);
    else
        unavailable.setBounds(bounds);
}
} // namespace sonicator::spike
