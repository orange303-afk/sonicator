#pragma once
#include "plugin/SpikeProcessor.h"
#include <memory>

namespace sonicator {
class MainComponent;
}

namespace sonicator::spike {
// Hosts the existing application window as a plugin editor. The spike intentionally has no audio
// path: the editor owns MainComponent, and the processor stays silent.
class SpikeEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SpikeEditor(SpikeProcessor&);
    ~SpikeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static void logEvent(const juce::String& event);
    static juce::File spikeFolder();

    std::unique_ptr<sonicator::MainComponent> main;
    juce::Label unavailable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpikeEditor)
};
} // namespace sonicator::spike
