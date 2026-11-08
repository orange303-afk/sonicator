#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
// Full-height progress/scrub bar for a whole tracker composition. It replaces the empty waveform
// area until a source sample is selected; click or drag anywhere to seek.
class SongProgress final : public juce::Component
{
public:
    void setLength(double seconds);
    void setPosition(double seconds);
    double length() const { return lengthSeconds; }
    double position() const { return positionSeconds; }
    // Minutes:seconds:milliseconds, e.g. 06:54:320.
    static juce::String formatTime(double seconds);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    std::function<void(double)> onSeek;
private:
    double lengthSeconds = 0, positionSeconds = 0;
    void seekAt(int x);
};
} // namespace sonicator
