#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "WaveformSelection.h"
namespace sonicator {
class WaveformDisplay final : public juce::Component, private juce::ChangeListener
{
public:
    explicit WaveformDisplay(const juce::File& cacheFolder);
    ~WaveformDisplay() override;
    void setFile(const juce::File&);
    void setPosition(double p) { position = p; repaint(); }
    void setSelection(selection::Range);
    selection::Range selection() const { return currentSelection; }
    void clearSelection();
    double totalLength() const { return thumbnail.getTotalLength(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    std::function<void(double)> onSeek;
    std::function<void(selection::Range)> onSelectionChanged;
    std::function<void()> onExternalDragRequested;
private:
    juce::File cacheFile;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 64 };
    juce::AudioThumbnail thumbnail { 512, formats, cache };
    double position = 0;
    selection::Range currentSelection;
    juce::Point<int> dragOrigin;
    double dragAnchor = 0;
    bool pressInsideSelection = false, selectionDragActive = false, externalDragTriggered = false;
    void updateSelectionDuringDrag(double time);
    void notifySelection();
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
};
} // namespace sonicator
