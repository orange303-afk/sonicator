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
    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    AudioEngine& engine;
    WaveformDisplay waveform;
    juce::Label filename, time;
    juce::TextButton play { "Play" }, stop { "Stop" };
    juce::ToggleButton loop { "Loop" };
    juce::Slider volume;
    void timerCallback() override;
    void togglePlayback();
};
}
