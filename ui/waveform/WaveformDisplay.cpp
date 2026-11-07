#include "WaveformDisplay.h"
#include <algorithm>
#include "ui/shell/Theme.h"
#include <cmath>
namespace sonicator {
namespace
{
bool sameRange(selection::Range a, selection::Range b)
{
    return a.start == b.start && a.end == b.end;
}
}
WaveformDisplay::WaveformDisplay(const juce::File& folder) : cacheFile(folder.getChildFile("waveforms.cache"))
{
    formats.registerBasicFormats(); thumbnail.addChangeListener(this);
    folder.createDirectory();
    if (auto stream = cacheFile.createInputStream()) cache.readFromStream(*stream);
}
WaveformDisplay::~WaveformDisplay()
{
    thumbnail.removeChangeListener(this);
    thumbnail.setSource(nullptr);
    juce::TemporaryFile temporary(cacheFile);
    if (auto stream = temporary.getFile().createOutputStream())
    {
        cache.writeToStream(*stream); stream->flush();
        if (stream->getStatus().wasOk()) { stream.reset(); temporary.overwriteTargetFileWithTemporary(); }
    }
}
void WaveformDisplay::setFile(const juce::File& file)
{
    position = 0;
    peakMin.clear(); peakMax.clear(); sliceTimes.clear(); activeSlices.clear(); customLength = 0;
    clearSelection();
    if (file.existsAsFile()) thumbnail.setSource(new juce::FileInputSource(file,true));
    else thumbnail.setSource(nullptr);
    repaint();
}
void WaveformDisplay::setPeaks(const std::vector<float>& samples, int sampleRate,
                               const std::vector<int64_t>& sliceStarts)
{
    position = 0;
    clearSelection();
    thumbnail.setSource(nullptr);
    peakMin.clear(); peakMax.clear(); sliceTimes.clear(); activeSlices.clear();
    customLength = sampleRate > 0 ? (double) samples.size() / sampleRate : 0;
    if (samples.empty() || customLength <= 0) { repaint(); return; }
    constexpr int bins = 2048;
    peakMin.assign((size_t) bins, 0.0f);
    peakMax.assign((size_t) bins, 0.0f);
    for (int bin = 0; bin < bins; ++bin)
    {
        const auto begin = (size_t) ((int64_t) samples.size() * bin / bins);
        const auto end = (size_t) ((int64_t) samples.size() * (bin + 1) / bins);
        float low = 0, high = 0;
        for (size_t i = begin; i < end; ++i)
        {
            low = juce::jmin(low, samples[i]);
            high = juce::jmax(high, samples[i]);
        }
        peakMin[(size_t) bin] = low;
        peakMax[(size_t) bin] = high;
    }
    for (auto start : sliceStarts)
        sliceTimes.push_back((double) start / sampleRate);
    repaint();
}
void WaveformDisplay::setActiveSlices(std::vector<int> indices)
{
    if (indices == activeSlices) return;
    activeSlices = std::move(indices);
    repaint();
}
void WaveformDisplay::setSelection(selection::Range r)
{
    if (! std::isfinite(r.start) || ! std::isfinite(r.end))
    {
        r = {};
    }
    else
    {
        auto lo = juce::jmin(r.start, r.end), hi = juce::jmax(r.start, r.end);
        if (totalLength() > 0)
        {
            lo = juce::jlimit(0.0, totalLength(), lo);
            hi = juce::jlimit(0.0, totalLength(), hi);
        }
        lo = juce::jmax(0.0, lo);
        r = hi > lo ? selection::Range { lo, hi } : selection::Range {};
    }
    if (sameRange(r, currentSelection)) return;
    currentSelection = r;
    repaint();
    notifySelection();
}
void WaveformDisplay::clearSelection()
{
    if (! currentSelection.valid())
    {
        currentSelection = {};
        return;
    }
    currentSelection = {};
    repaint();
    notifySelection();
}
void WaveformDisplay::notifySelection()
{
    if (onSelectionChanged) onSelectionChanged(currentSelection);
}
void WaveformDisplay::updateSelectionDuringDrag(double time)
{
    currentSelection = selection::orderedRange(dragAnchor, time, totalLength());
    repaint();
}
void WaveformDisplay::paint(juce::Graphics& g)
{
    g.setColour(theme::table); g.fillRect(getLocalBounds());
    const auto total = totalLength();
    if (total <= 0)
    {
        g.setColour(theme::muted); g.drawText("Select a sample to preview",getLocalBounds(),juce::Justification::centred); return;
    }
    const auto area = getLocalBounds().reduced(6,10).toFloat();
    if (! peakMin.empty())
    {
        const auto centre = area.getCentreY();
        const auto half = area.getHeight() * 0.5f;
        g.setColour(theme::accent);
        for (int bin = 0; bin < (int) peakMin.size(); ++bin)
        {
            const auto x = area.getX() + area.getWidth() * ((float) bin + 0.5f) / (float) peakMin.size();
            g.drawVerticalLine((int) x, centre - peakMax[(size_t) bin] * half,
                               centre - peakMin[(size_t) bin] * half);
        }
        for (size_t i = 0; i < sliceTimes.size(); ++i)
        {
            const auto x = selection::xAtTime(sliceTimes[i], getWidth(), total);
            g.setColour(theme::muted.withAlpha(0.55f));
            g.drawVerticalLine((int) x, area.getY(), area.getBottom());
            if (std::find(activeSlices.begin(), activeSlices.end(), (int) i) != activeSlices.end())
            {
                const auto next = i + 1 < sliceTimes.size() ? sliceTimes[i + 1] : total;
                const auto x2 = selection::xAtTime(next, getWidth(), total);
                g.setColour(theme::accent.withAlpha(0.28f));
                g.fillRect(juce::Rectangle<float>(x, area.getY(), juce::jmax(1.0f, x2 - x), area.getHeight()));
            }
        }
        g.setColour(theme::accent);
    }
    else
    {
        g.setColour(theme::accent);
        thumbnail.drawChannels(g,area.toNearestInt(),0,total,0.85f);
    }
    if (currentSelection.valid())
    {
        const auto x1 = selection::xAtTime(currentSelection.start, getWidth(), total);
        const auto x2 = selection::xAtTime(currentSelection.end, getWidth(), total);
        g.setColour(theme::accent.withAlpha(0.20f));
        g.fillRect(juce::Rectangle<float>(x1, 6.0f, juce::jmax(1.0f, x2 - x1), (float) getHeight() - 12.0f));
        g.setColour(theme::accent.withAlpha(0.85f));
        g.drawVerticalLine((int) x1, 5.0f, (float) getHeight() - 5.0f);
        g.drawVerticalLine((int) x2, 5.0f, (float) getHeight() - 5.0f);
        const auto label = selection::formatRange(currentSelection);
        g.setFont(juce::FontOptions(11));
        const auto labelWidth = 104;
        const auto labelX = juce::jlimit(6.0f, juce::jmax(6.0f, (float) getWidth() - labelWidth - 6.0f), x1 + 4.0f);
        const juce::Rectangle<float> labelBounds(labelX, 6.0f, (float) labelWidth, 18.0f);
        g.setColour(theme::panel);
        g.fillRoundedRectangle(labelBounds, 4.0f);
        g.setColour(theme::text);
        g.drawText(label, labelBounds, juce::Justification::centred);
    }
    float progress = 6 + (float) juce::jlimit(0.0,1.0,position/total) * (getWidth()-12);
    g.setColour(theme::accent); g.drawVerticalLine((int) progress,5,(float) getHeight()-5);
}
void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    externalDragTriggered = false;
    selectionDragActive = false;
    pressInsideSelection = false;
    dragOrigin = e.getPosition();
    const auto total = totalLength();
    if (total <= 0) return;
    dragAnchor = selection::timeAtX(e.position.x, getWidth(), total);
    pressInsideSelection = selection::contains(currentSelection, dragAnchor);
}
void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const auto total = totalLength();
    if (total <= 0) return;
    const auto delta = e.getPosition() - dragOrigin;
    if (pressInsideSelection)
    {
        if (! externalDragTriggered && selection::exceedsDragThreshold(delta.x, delta.y))
        {
            externalDragTriggered = true;
            if (onExternalDragRequested) onExternalDragRequested();
        }
        return;
    }
    if (! selectionDragActive)
    {
        if (! selection::exceedsDragThreshold(delta.x, delta.y)) return;
        selectionDragActive = true;
    }
    updateSelectionDuringDrag(selection::timeAtX(e.position.x, getWidth(), total));
}
void WaveformDisplay::mouseUp(const juce::MouseEvent& e)
{
    const auto total = totalLength();
    if (total > 0 && ! selectionDragActive && ! externalDragTriggered)
    {
        if (onSeek) onSeek(selection::timeAtX(e.position.x, getWidth(), total));
        clearSelection();
    }
    if (selectionDragActive)
    {
        selectionDragActive = false;
        if (currentSelection.valid()) notifySelection();
        else clearSelection();
    }
    pressInsideSelection = false;
    externalDragTriggered = false;
}
} // namespace sonicator
