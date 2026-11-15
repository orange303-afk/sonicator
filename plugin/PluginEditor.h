#pragma once
#include "plugin/PluginProcessor.h"
#include <memory>

namespace sonicator { class MainComponent; }

namespace sonicator::plugin
{
// Hosts MainComponent inside the DAW editor. The processor keeps the engine and the project state;
// the editor is created and destroyed as the host opens and closes the window.
class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processor;
    std::unique_ptr<sonicator::MainComponent> main;
    juce::Label unavailable;
    // The host may restore project state while this window is open; the older selection must not
    // be written back over it when the window closes.
    int stateGenerationAtOpen = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
} // namespace sonicator::plugin
