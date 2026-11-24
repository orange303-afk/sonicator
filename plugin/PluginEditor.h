#pragma once
#include "plugin/PluginProcessor.h"
#include <memory>

namespace sonicator { class MainComponent; }

namespace sonicator::plugin
{
// Hosts MainComponent inside the DAW editor. The processor keeps the engine and the project state;
// the editor is created and destroyed as the host opens and closes the window.
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    // A click that lands in the window also asks the window for keyboard focus: some Windows hosts
    // (Fender Studio Pro / Studio One reported it on 0.2.0) deliver the click but leave keys with the
    // arrangement, so typing only worked through the clipboard.
    void mouseDown(const juce::MouseEvent&) override;

private:
    PluginProcessor& processor;
    std::unique_ptr<sonicator::MainComponent> main;
    juce::Label unavailable;
    // The host may restore project state while this window is open; the older selection must not
    // be written back over it when the window closes.
    int stateGenerationAtOpen = 0;
    float editorScale = 1.0f;
    void applyInterfaceScale(float);
    bool initialising = true, restoringProjectSize = false, windowSizeDirty = false;
    int rememberedWidth = 0, rememberedHeight = 0;
    juce::uint32 lastWindowResize = 0;
    void persistWindowSize();

    // Replaces the browser with an explanation when the shared catalog cannot be opened.
    void showCatalogProblem(const juce::String& message, const juce::String& componentId);
    // Applies a project state the host loaded into this still-open window.
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
} // namespace sonicator::plugin
