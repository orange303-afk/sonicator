#include "SongProgress.h"
#include "ui/shell/Theme.h"
#include <cmath>
namespace sonicator {
juce::String SongProgress::formatTime(double seconds)
{
    if (! std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const auto totalMs = (int64_t) std::llround(seconds * 1000.0);
    const auto minutes = totalMs / 60000;
    const auto secs = (totalMs / 1000) % 60;
    const auto millis = totalMs % 1000;
    const auto two = [] (int64_t value) { return juce::String(value).paddedLeft('0', 2); };
    return two(minutes) + ":" + two(secs) + ":" + juce::String(millis).paddedLeft('0', 3);
}

void SongProgress::setLength(double seconds)
{
    lengthSeconds = std::isfinite(seconds) && seconds > 0 ? seconds : 0.0;
    positionSeconds = juce::jlimit(0.0, lengthSeconds, positionSeconds);
    repaint();
}

void SongProgress::setPosition(double seconds)
{
    const auto next = std::isfinite(seconds) ? juce::jlimit(0.0, lengthSeconds, seconds) : 0.0;
    if (std::abs(next - positionSeconds) < 1.0e-6) return;
    positionSeconds = next;
    repaint();
}

void SongProgress::seekAt(int x)
{
    if (lengthSeconds <= 0 || getWidth() <= 0 || onSeek == nullptr) return;
    const auto proportion = juce::jlimit(0.0, 1.0, (double) x / (double) getWidth());
    onSeek(proportion * lengthSeconds);
}

void SongProgress::mouseDown(const juce::MouseEvent& event) { seekAt(event.x); }
void SongProgress::mouseDrag(const juce::MouseEvent& event) { seekAt(event.x); }

void SongProgress::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour(theme::table);
    g.fillRoundedRectangle(area, 10.0f);
    g.setColour(theme::line);
    g.drawRoundedRectangle(area.reduced(0.5f), 10.0f, 1.0f);

    const auto proportion = lengthSeconds > 0
        ? (float) juce::jlimit(0.0, 1.0, positionSeconds / lengthSeconds) : 0.0f;
    if (proportion > 0.0f)
    {
        g.setColour(theme::accent.withAlpha(0.20f));
        g.fillRoundedRectangle(area.withWidth(area.getWidth() * proportion), 10.0f);
    }
    const float playhead = area.getX() + area.getWidth() * proportion;
    g.setColour(theme::accent);
    g.fillRect(juce::Rectangle<float>(playhead - 1.0f, area.getY(), 2.0f, area.getHeight()));

    g.setColour(theme::text);
    g.setFont(juce::FontOptions(15.0f));
    g.drawText(formatTime(positionSeconds) + " / " + formatTime(lengthSeconds),
               getLocalBounds(), juce::Justification::centred, false);
}
} // namespace sonicator
