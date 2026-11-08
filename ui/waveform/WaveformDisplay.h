#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include "WaveformSelection.h"
namespace sonicator {
class WaveformDisplay final : public juce::Component, private juce::ChangeListener
{
public:
    explicit WaveformDisplay(const juce::File& cacheFolder);
    ~WaveformDisplay() override;
    void setFile(const juce::File&);
    // REX2 and other decoded containers have no file the thumbnail can read, so their PCM is
    // reduced to min/max peaks here. The caller keeps ownership of the samples.
    void setPeaks(const std::vector<float>& samples, int sampleRate,
                  const std::vector<int64_t>& sliceStarts);
    void setActiveSlices(std::vector<int> indices);
    void setPosition(double p) { position = p; repaint(); }
    void setSelection(selection::Range);
    selection::Range selection() const { return currentSelection; }
    void clearSelection();
    double totalLength() const { return customLength > 0 ? customLength : thumbnail.getTotalLength(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void(double)> onSeek;
    std::function<void(selection::Range)> onSelectionChanged;
    std::function<void()> onExternalDragRequested;
private:
    juce::File cacheFile;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 64 };
    juce::AudioThumbnail thumbnail { 512, formats, cache };
    std::vector<float> peakMin, peakMax;
    std::vector<double> sliceTimes;
    std::vector<int> activeSlices;
    double customLength = 0;
    double position = 0;
    selection::Range currentSelection;
    juce::Point<int> dragOrigin;
    double dragAnchor = 0;
    bool pressInsideSelection = false, selectionDragActive = false, externalDragTriggered = false;
    bool pressWasPrimary = false;
    void updateSelectionDuringDrag(double time);
    void notifySelection();
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
};
} // namespace sonicator
