#include "WaveformDisplay.h"
#include "core/audio/SonicatorFormats.h"
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
    registerSonicatorFormats(formats); thumbnail.addChangeListener(this);
    folder.createDirectory();
    if (auto stream = cacheFile.createInputStream()) cache.readFromStream(*stream);
    // Edit keys belong to the preview: clicking the wave puts the keyboard focus there instead of
    // leaving it on the library list, where the same letters are ordinary typing/search input.
    setWantsKeyboardFocus(true);
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
    editedAudio.reset(); editedSlices.clear(); columnCache.clear(); cachedWidth = 0;
    clearSelection();
    viewRange = {};
    zoomReader.reset();
    zoomSampleRate = 0;
    if (file.existsAsFile())
    {
        thumbnail.setSource(new juce::FileInputSource(file, true));
        zoomReader.reset(formats.createReaderFor(file));
        if (zoomReader != nullptr) zoomSampleRate = (int) std::lround(zoomReader->sampleRate);
    }
    else
        thumbnail.setSource(nullptr);
    repaint();
}
void WaveformDisplay::setPeaks(const std::vector<float>& samples, int sampleRate,
                               const std::vector<int64_t>& sliceStarts)
{
    position = 0;
    clearSelection();
    viewRange = {};
    thumbnail.setSource(nullptr);
    peakMin.clear(); peakMax.clear(); sliceTimes.clear(); activeSlices.clear();
    editedAudio.reset(); editedSlices.clear(); columnCache.clear(); cachedWidth = 0;
    zoomReader.reset();
    zoomSampleRate = sampleRate;
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
void WaveformDisplay::setEditedAudio(SampleAudioPtr audio, const std::vector<int64_t>& sliceStarts)
{
    editedAudio = std::move(audio);
    editedSlices = sliceStarts;
    columnCache.clear(); cachedWidth = 0;
    // An edit keeps the zoomed view (clamped to the new length); only a source change resets it.
    if (viewRange.valid()) viewRange = waveview::clampView(viewRange, totalLength());
    if (editedAudio != nullptr && editedAudio->sampleRate > 0)
        setSampleRateForZoom(editedAudio->sampleRate);
    repaint();
}

void WaveformDisplay::clearEditedAudio()
{
    editedAudio.reset();
    editedSlices.clear();
    columnCache.clear(); cachedWidth = 0;
    repaint();
}

void WaveformDisplay::drawFileSamples(juce::Graphics& g, juce::Rectangle<float> area,
                                     waveview::View view)
{
    if (zoomReader == nullptr || zoomSampleRate <= 0 || ! view.valid()) return;
    const auto rate = (double) zoomSampleRate;
    const auto totalFrames = zoomReader->lengthInSamples;
    const auto first = juce::jlimit<int64_t>(0, totalFrames - 1, (int64_t) std::floor(view.start * rate));
    const auto last = juce::jlimit<int64_t>(first + 1, totalFrames, (int64_t) std::ceil(view.end * rate));
    const auto count = last - first;
    if (count <= 0 || count > 2 * 1024 * 1024) return;
    const int channels = juce::jmin(2, (int) zoomReader->numChannels);
    if (channels <= 0) return;
    juce::AudioBuffer<float> buffer(channels, (int) count);
    if (! zoomReader->read(&buffer, 0, (int) count, first, true, channels > 1)) return;
    const int width = juce::jmax(1, (int) area.getWidth());
    const bool stereo = channels > 1;
    const auto laneHeight = area.getHeight() * (stereo ? 0.5f : 1.0f);
    const auto leftCentre = stereo ? area.getY() + laneHeight * 0.5f : area.getCentreY();
    const auto rightCentre = area.getY() + laneHeight * 1.5f;
    const auto scale = laneHeight * 0.5f;
    g.setColour(theme::accent);
    for (int x = 0; x < width; ++x)
    {
        const auto from = (int64_t) ((int64_t) count * x / width);
        const auto to = juce::jmax<int64_t>(from + 1, (int64_t) count * (x + 1) / width);
        float minLeft = 0, maxLeft = 0, minRight = 0, maxRight = 0;
        for (int64_t i = from; i < to; ++i)
        {
            minLeft = juce::jmin(minLeft, buffer.getSample(0, (int) i));
            maxLeft = juce::jmax(maxLeft, buffer.getSample(0, (int) i));
            if (! stereo) continue;
            minRight = juce::jmin(minRight, buffer.getSample(1, (int) i));
            maxRight = juce::jmax(maxRight, buffer.getSample(1, (int) i));
        }
        const auto px = area.getX() + (float) x;
        g.drawVerticalLine((int) px, leftCentre - maxLeft * scale, leftCentre - minLeft * scale);
        if (stereo)
            g.drawVerticalLine((int) px, rightCentre - maxRight * scale, rightCentre - minRight * scale);
    }
}

void WaveformDisplay::drawEditedPeaks(juce::Graphics& g, juce::Rectangle<float> area, waveview::View view)
{
    if (editedAudio == nullptr || ! view.valid() || editedAudio->frames <= 0) return;
    const int width = juce::jmax(1, (int) area.getWidth());
    if (cachedWidth != width || cachedView.start != view.start || cachedView.end != view.end)
    {
        columnCache.assign((size_t) width, {});
        const auto frames = editedAudio->frames;
        const auto rate = (double) editedAudio->sampleRate;
        const bool stereo = editedAudio->channels > 1;
        for (int x = 0; x < width; ++x)
        {
            const auto t0 = view.start + view.length() * (double) x / (double) width;
            const auto t1 = view.start + view.length() * (double) (x + 1) / (double) width;
            const auto first = juce::jlimit<int64_t>(0, frames - 1, (int64_t) std::floor(t0 * rate));
            const auto last = juce::jlimit<int64_t>(first + 1, frames,
                                                    (int64_t) std::ceil(t1 * rate));
            auto& column = columnCache[(size_t) x];
            column = {};
            for (int64_t i = first; i < last; ++i)
            {
                const auto l = editedAudio->left[(size_t) i];
                column.minLeft = juce::jmin(column.minLeft, l);
                column.maxLeft = juce::jmax(column.maxLeft, l);
                if (! stereo) continue;
                const auto r = editedAudio->right[(size_t) i];
                column.minRight = juce::jmin(column.minRight, r);
                column.maxRight = juce::jmax(column.maxRight, r);
            }
        }
        cachedView = view;
        cachedWidth = width;
    }
    const bool stereo = editedAudio->channels > 1;
    const auto laneHeight = area.getHeight() * (stereo ? 0.5f : 1.0f);
    const auto laneTop = area.getY();
    const auto leftCentre = stereo ? laneTop + laneHeight * 0.5f : area.getCentreY();
    const auto rightCentre = laneTop + laneHeight * 1.5f;
    const auto scale = laneHeight * 0.5f;
    g.setColour(theme::accent);
    for (int x = 0; x < (int) columnCache.size(); ++x)
    {
        const auto& column = columnCache[(size_t) x];
        const auto px = area.getX() + (float) x;
        g.drawVerticalLine((int) px, leftCentre - column.maxLeft * scale,
                           leftCentre - column.minLeft * scale);
        if (stereo)
            g.drawVerticalLine((int) px, rightCentre - column.maxRight * scale,
                               rightCentre - column.minRight * scale);
    }
    if (! editedSlices.empty() && editedAudio->sampleRate > 0)
    {
        g.setColour(theme::muted.withAlpha(0.55f));
        for (const auto frame : editedSlices)
        {
            const auto x = waveview::xAtTime((double) frame / (double) editedAudio->sampleRate,
                                             getWidth(), view);
            g.drawVerticalLine((int) x, area.getY(), area.getBottom());
        }
    }
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
waveview::View WaveformDisplay::effectiveView() const
{
    const auto total = totalLength();
    if (! viewRange.valid()) return waveview::whole(total);
    return waveview::clampView(viewRange, total);
}

double WaveformDisplay::minimumViewLength() const
{
    const auto total = totalLength();
    if (zoomSampleRate > 0 && getWidth() > 0) return (double) getWidth() / (double) zoomSampleRate;
    return total > 0 ? total / 1000.0 : 0.0;
}

void WaveformDisplay::setView(waveview::View view)
{
    const auto total = totalLength();
    viewRange = waveview::clampView(view, total);
    repaint();
}

void WaveformDisplay::zoomToFit()
{
    viewRange = waveview::whole(totalLength());
    repaint();
}

void WaveformDisplay::zoomToSelection()
{
    if (! currentSelection.valid()) return;
    setView({ currentSelection.start, currentSelection.end });
}

void WaveformDisplay::zoomAtTime(double seconds, double factor)
{
    const auto total = totalLength();
    if (! (total > 0) || ! (factor > 0)) return;
    setView(waveview::zoomAround(effectiveView(), seconds, factor, total, minimumViewLength()));
}

void WaveformDisplay::panBySeconds(double seconds)
{
    setView(waveview::panBy(effectiveView(), seconds, totalLength()));
}

bool WaveformDisplay::isOverviewPoint(juce::Point<int> point) const
{
    return point.y >= getHeight() - 14
        && ! waveview::isWholeFile(effectiveView(), totalLength());
}

void WaveformDisplay::notifySelection()
{
    if (onSelectionChanged) onSelectionChanged(currentSelection);
}
void WaveformDisplay::updateSelectionDuringDrag(double time)
{
    publishLiveSelection(selection::orderedRange(dragAnchor, time, totalLength()));
}
void WaveformDisplay::publishLiveSelection(selection::Range r)
{
    if (! r.valid() || sameRange(r, currentSelection)) return;
    currentSelection = r;
    repaint();
    notifySelectionChanging();
}
void WaveformDisplay::notifySelectionChanging()
{
    if (onSelectionChanging) onSelectionChanging(currentSelection);
}
double WaveformDisplay::secondsPerPixel() const
{
    const auto view = effectiveView();
    const double track = (double) getWidth() - 2.0 * selection::inset;
    return track > 0 && view.length() > 0 ? view.length() / track : 0.0;
}
double WaveformDisplay::minimumSelectionLength() const
{
    const auto timePerPixel = secondsPerPixel();
    return timePerPixel > 0 ? timePerPixel : 0.001;
}
void WaveformDisplay::paint(juce::Graphics& g)
{
    g.setColour(theme::table); g.fillRect(getLocalBounds());
    const auto total = totalLength();
    if (total <= 0)
    {
        g.setColour(theme::muted); g.drawText("Select a sample to preview",getLocalBounds(),juce::Justification::centred); return;
    }
    const auto view = effectiveView();
    const auto area = getLocalBounds().reduced(6,10).toFloat();
    if (editedAudio != nullptr)
    {
        drawEditedPeaks(g, area, view);
    }
    else if (! peakMin.empty())
    {
        const auto centre = area.getCentreY();
        const auto half = area.getHeight() * 0.5f;
        g.setColour(theme::accent);
        for (int bin = 0; bin < (int) peakMin.size(); ++bin)
        {
            const auto binStart = total * bin / (double) peakMin.size();
            const auto binEnd = total * (bin + 1) / (double) peakMin.size();
            if (binEnd <= view.start || binStart >= view.end) continue;
            const auto x = waveview::xAtTime((binStart + binEnd) * 0.5, getWidth(), view);
            g.drawVerticalLine((int) x, centre - peakMax[(size_t) bin] * half,
                               centre - peakMin[(size_t) bin] * half);
        }
        for (size_t i = 0; i < sliceTimes.size(); ++i)
        {
            const auto x = waveview::xAtTime(sliceTimes[i], getWidth(), view);
            g.setColour(theme::muted.withAlpha(0.55f));
            g.drawVerticalLine((int) x, area.getY(), area.getBottom());
            if (std::find(activeSlices.begin(), activeSlices.end(), (int) i) != activeSlices.end())
            {
                const auto next = i + 1 < sliceTimes.size() ? sliceTimes[i + 1] : total;
                const auto x2 = waveview::xAtTime(next, getWidth(), view);
                g.setColour(theme::accent.withAlpha(0.28f));
                g.fillRect(juce::Rectangle<float>(x, area.getY(), juce::jmax(1.0f, x2 - x), area.getHeight()));
            }
        }
        g.setColour(theme::accent);
    }
    else
    {
        g.setColour(theme::accent);
        const auto framesPerPixel = zoomSampleRate > 0
            ? view.length() * (double) zoomSampleRate / (double) juce::jmax(1, getWidth()) : 0.0;
        if (zoomReader != nullptr && framesPerPixel < 16.0)
            drawFileSamples(g, area, view);
        else
            thumbnail.drawChannels(g,area.toNearestInt(),view.start,view.end,0.85f);
    }
    if (currentSelection.valid())
    {
        const auto x1 = waveview::xAtTime(currentSelection.start, getWidth(), view);
        const auto x2 = waveview::xAtTime(currentSelection.end, getWidth(), view);
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
    const auto progress = waveview::xAtTime(position, getWidth(), view);
    g.setColour(theme::accent); g.drawVerticalLine((int) progress,5,(float) getHeight()-5);
    if (! waveview::isWholeFile(view, total))
    {
        const juce::Rectangle<float> strip((float) selection::inset, (float) getHeight() - 12.0f,
                                           (float) getWidth() - 2.0f * (float) selection::inset, 8.0f);
        g.setColour(theme::panel); g.fillRoundedRectangle(strip, 2.0f);
        const auto frameStart = strip.getX() + strip.getWidth() * (float) (view.start / total);
        const auto frameEnd = strip.getX() + strip.getWidth() * (float) (view.end / total);
        g.setColour(theme::accent.withAlpha(0.55f));
        g.fillRoundedRectangle(juce::Rectangle<float>(frameStart, strip.getY(),
            juce::jmax(2.0f, frameEnd - frameStart), strip.getHeight()), 2.0f);
    }
}
void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.mods.isPopupMenu() || e.mods.isRightButtonDown())
    {
        contextMenuClick = true;
        if (onContextMenu) onContextMenu(e.getScreenPosition());
        return;
    }
    externalDragTriggered = false;
    selectionDragActive = false;
    pressInsideSelection = false;
    pressWasPrimary = ! e.mods.isPopupMenu() && ! e.mods.isMiddleButtonDown();
    dragOrigin = e.getPosition();
    dragMode = DragMode::none;
    const auto total = totalLength();
    if (total <= 0) return;
    const auto view = effectiveView();
    if (isOverviewPoint(e.getPosition()))
    {
        dragMode = DragMode::overview;
        overviewDragStart = view.start;
        return;
    }
    dragAnchor = waveview::timeAtX(e.position.x, getWidth(), view);
    pressInsideSelection = selection::contains(currentSelection, dragAnchor);
    if (! pressWasPrimary || ! loopArmed || ! currentSelection.valid()) return;
    // Cmd/Ctrl-drag still starts the external file drag that plain dragging inside used to do.
    if (pressInsideSelection && e.mods.isCommandDown()) return;
    // Edges win over the body: a few pixels around either boundary resize, the rest moves the
    // whole window. Both are live, so a playing loop follows the mouse.
    const auto tolerance = 5.0 * secondsPerPixel();
    if (std::abs(dragAnchor - currentSelection.start) <= tolerance) dragMode = DragMode::resizeStart;
    else if (std::abs(dragAnchor - currentSelection.end) <= tolerance) dragMode = DragMode::resizeEnd;
    else if (pressInsideSelection) dragMode = DragMode::move;
    if (dragMode != DragMode::none) { dragStartRange = currentSelection; dragStartTime = dragAnchor; }
}
void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const auto total = totalLength();
    if (total <= 0) return;
    const auto delta = e.getPosition() - dragOrigin;
    if (dragMode == DragMode::move)
    {
        if (! selection::exceedsDragThreshold(delta.x, delta.y)) return;
        const double movedBy = waveview::timeAtX(e.position.x, getWidth(), effectiveView()) - dragStartTime;
        const double length = dragStartRange.length();
        const double start = juce::jlimit(0.0, juce::jmax(0.0, total - length),
                                          dragStartRange.start + movedBy);
        publishLiveSelection({ start, start + length });
        return;
    }
    if (dragMode == DragMode::overview)
    {
        const auto track = juce::jmax(1.0f, (float) getWidth() - 2.0f * (float) selection::inset);
        const auto moved = (double) delta.x / (double) track * total;
        auto view = effectiveView();
        view.start = overviewDragStart + moved;
        view.end = view.start + view.length();
        setView(view);
        return;
    }
    if (dragMode == DragMode::resizeStart || dragMode == DragMode::resizeEnd)
    {
        if (! selection::exceedsDragThreshold(delta.x, delta.y)) return;
        const double time = waveview::timeAtX(e.position.x, getWidth(), effectiveView());
        const double minLength = minimumSelectionLength();
        selection::Range r = dragStartRange;
        if (dragMode == DragMode::resizeStart)
            r.start = juce::jlimit(0.0, dragStartRange.end - minLength, time);
        else
            r.end = juce::jlimit(dragStartRange.start + minLength, total, time);
        if (r.end > r.start) publishLiveSelection(r);
        return;
    }
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
        dragMode = DragMode::select;
    }
    updateSelectionDuringDrag(waveview::timeAtX(e.position.x, getWidth(), effectiveView()));
}
void WaveformDisplay::mouseUp(const juce::MouseEvent& e)
{
    // A right-click opens the menu; its release must not seek, clear the selection or start a
    // selection drag that the popup already consumed.
    if (contextMenuClick || e.mods.isPopupMenu() || e.mods.isRightButtonDown())
    {
        contextMenuClick = false;
        dragMode = DragMode::none;
        pressWasPrimary = false;
        return;
    }
    const auto total = totalLength();
    // JUCE delivers the second mouseUp of a double-click before mouseDoubleClick, with two clicks
    // recorded. That release must not seek or clear the range mouseDoubleClick is about to make.
    const bool completesDoubleClick = pressWasPrimary && e.getNumberOfClicks() >= 2;
    const bool editedRange = dragMode == DragMode::move || dragMode == DragMode::resizeStart
        || dragMode == DragMode::resizeEnd || dragMode == DragMode::overview;
    if (total > 0 && ! completesDoubleClick && ! selectionDragActive && ! editedRange
        && ! externalDragTriggered)
    {
        if (onSeek) onSeek(waveview::timeAtX(e.position.x, getWidth(), effectiveView()));
        // A click inside an armed loop seeks within it; dragging is how the range itself is moved.
        if (! (loopArmed && pressInsideSelection)) clearSelection();
    }
    if (selectionDragActive || editedRange)
    {
        selectionDragActive = false;
        if (currentSelection.valid()) notifySelection();
        else clearSelection();
    }
    dragMode = DragMode::none;
    pressInsideSelection = false;
    externalDragTriggered = false;
}
void WaveformDisplay::mouseMove(const juce::MouseEvent& e)
{
    const auto total = totalLength();
    if (total <= 0) { setMouseCursor(juce::MouseCursor::NormalCursor); return; }
    updateCursorFor(waveview::timeAtX(e.position.x, getWidth(), effectiveView()));
}
void WaveformDisplay::updateCursorFor(double time)
{
    if (! loopArmed || ! currentSelection.valid())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    const auto tolerance = 5.0 * secondsPerPixel();
    if (std::abs(time - currentSelection.start) <= tolerance
        || std::abs(time - currentSelection.end) <= tolerance)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (selection::contains(currentSelection, time))
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}
void WaveformDisplay::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto total = totalLength();
    if (! (total > 0)) return;
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (std::abs(delta) < 0.0001f) return;
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        const auto factor = juce::jlimit(0.5, 2.0, std::pow(1.5, (double) delta));
        zoomAtTime(waveview::timeAtX(event.position.x, getWidth(), effectiveView()), factor);
        return;
    }
    if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        panBySeconds(-(double) delta * effectiveView().length() * 0.5);
        return;
    }
    if (! currentSelection.valid())
    {
        panBySeconds(-(double) delta * effectiveView().length() * 0.5);
        return;
    }
    // One notch is a moderate step; smooth trackpads send many small events, so the exponent keeps
    // the gesture proportional instead of snapping the range to the whole file.
    const auto factor = juce::jlimit(0.8, 1.25, std::pow(1.5, (double) delta));
    const auto centre = (currentSelection.start + currentSelection.end) * 0.5;
    const auto length = juce::jlimit(minimumSelectionLength(), total, currentSelection.length() * factor);
    const auto start = juce::jlimit(0.0, juce::jmax(0.0, total - length), centre - length * 0.5);
    publishLiveSelection({ start, start + length });
}
void WaveformDisplay::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    if (totalLength() <= 0 || ! (scaleFactor > 0)) return;
    zoomAtTime(waveview::timeAtX(event.position.x, getWidth(), effectiveView()),
               (double) scaleFactor);
}

void WaveformDisplay::mouseDoubleClick(const juce::MouseEvent&)
{
    // The whole displayed source, end exclusive. setSelection publishes only a real change, so a
    // repeated double-click on an already full range does not restart export preparation.
    if (! pressWasPrimary) return;
    const auto total = totalLength();
    if (! (total > 0) || ! std::isfinite(total)) return;
    setSelection({ 0.0, total });
}
} // namespace sonicator
