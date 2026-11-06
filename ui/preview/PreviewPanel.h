#pragma once
#include "core/audio/AudioEngine.h"
#include "ui/waveform/WaveformDisplay.h"
namespace sonicator {
class PreviewPanel final : public juce::Component, private juce::Timer
{
public:
    PreviewPanel(AudioEngine&, const juce::File& cacheFolder);
    ~PreviewPanel() override { stopTimer(); }
    bool loadFile(const juce::File&);
    std::function<void()> onAction;
    void resized() override;
    void lookAndFeelChanged() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    AudioEngine& engine;
    juce::File loadedFile;
    int64_t loadedSize = 0, loadedModified = 0;
    WaveformDisplay waveform;
    juce::Label filename, time;
    juce::TextButton play { "Play" }, stop { "Stop" };
    juce::ToggleButton loop { "Loop" };
    juce::ToggleButton midi { "MIDI Preview" }, skipSilence { "Skip Silence" };
    juce::ToggleButton autoPreview { "Auto Preview" };
    juce::Slider volume;
    void timerCallback() override;
    void togglePlayback();
};
}
