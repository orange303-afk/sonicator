#include "WaveformDisplay.h"
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
    clearSelection();
    if (file.existsAsFile()) thumbnail.setSource(new juce::FileInputSource(file,true));
    else thumbnail.setSource(nullptr);
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
    g.setColour(theme::background); g.fillRoundedRectangle(getLocalBounds().toFloat(),7);
    if (thumbnail.getTotalLength() <= 0)
    {
        g.setColour(theme::muted); g.drawText("Select a sample to preview",getLocalBounds(),juce::Justification::centred); return;
    }
    g.setColour(theme::accent);
    thumbnail.drawChannels(g,getLocalBounds().reduced(6,10),0,thumbnail.getTotalLength(),0.85f);
    const auto total = thumbnail.getTotalLength();
    if (currentSelection.valid())
    {
        const auto x1 = selection::xAtTime(currentSelection.start, getWidth(), total);
        const auto x2 = selection::xAtTime(currentSelection.end, getWidth(), total);
        g.setColour(theme::accent.brighter(0.6f).withAlpha(0.28f));
        g.fillRect(juce::Rectangle<float>(x1, 6.0f, juce::jmax(1.0f, x2 - x1), (float) getHeight() - 12.0f));
        g.setColour(theme::accent.withAlpha(0.85f));
        g.drawVerticalLine((int) x1, 5.0f, (float) getHeight() - 5.0f);
        g.drawVerticalLine((int) x2, 5.0f, (float) getHeight() - 5.0f);
        const auto label = selection::formatRange(currentSelection);
        g.setFont(juce::FontOptions(11));
        const auto labelWidth = 104;
        const auto labelX = juce::jlimit(6.0f, juce::jmax(6.0f, (float) getWidth() - labelWidth - 6.0f), x1 + 4.0f);
        const juce::Rectangle<float> labelBounds(labelX, 6.0f, (float) labelWidth, 18.0f);
        g.setColour(theme::panel.withAlpha(0.88f));
        g.fillRoundedRectangle(labelBounds, 4.0f);
        g.setColour(theme::text);
        g.drawText(label, labelBounds, juce::Justification::centred);
    }
    float x = 6 + (float) juce::jlimit(0.0,1.0,position/thumbnail.getTotalLength()) * (getWidth()-12);
    g.setColour(theme::text); g.drawVerticalLine((int) x,5,(float) getHeight()-5);
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
