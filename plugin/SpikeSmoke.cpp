// Opens the spike editor once, with the real catalog copy, and quits after a few seconds.
// Purpose: catch crashes or exceptions in MainComponent construction before the plugin is
// handed to a DAW host, and record how long the editor takes to become ready.
#include "plugin/SpikeProcessor.h"
#include "plugin/SpikeEditor.h"
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {
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
        startTimer(4000);
    }

    void shutdown() override
    {
        editor.reset();
        processor.reset();
    }

    void timerCallback() override
    {
        stopTimer();
        quit();
    }

private:
    std::unique_ptr<sonicator::spike::SpikeProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};
} // namespace

START_JUCE_APPLICATION(SmokeApp)
