#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
namespace sonicator {
class WaveformDisplay final : public juce::Component, private juce::ChangeListener
{
public:
    explicit WaveformDisplay(const juce::File& cacheFolder);
    ~WaveformDisplay() override;
    void setFile(const juce::File&);
    void setPosition(double p) { position = p; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent& e) override { mouseDown(e); }
    std::function<void(double)> onSeek;
private:
    juce::File cacheFile;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 64 };
    juce::AudioThumbnail thumbnail { 512, formats, cache };
    double position = 0;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
};
}
