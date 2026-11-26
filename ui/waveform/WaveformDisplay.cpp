#include "WaveformDisplay.h"
#include "core/audio/SonicatorFormats.h"
#include "core/preview/SpectrogramService.h"
#include "core/spectral/SpectrogramCache.h"
#include "SpectrogramRenderer.h"
#include <algorithm>
#include <queue>
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"
#include <cmath>
namespace sonicator {
namespace
{
constexpr int toolChipWidth = 34, toolChipGap = 3, spectralToolbarHeight = 18;
constexpr int spectralToolbarCount = 4;
const char* toolLabel(int index)
{
    static const char* labels[spectralToolbarCount] = { "Time", "Rect", "Lasso", "Wand" };
    return labels[juce::jlimit(0, spectralToolbarCount - 1, index)];
}

bool gridCovers(const SpectrogramGrid& grid, waveview::View view)
{
    return grid.startSeconds <= view.start + 1.0e-6 && grid.endSeconds >= view.end - 1.0e-6;
}

// Draws `image`, which spans [start, end] seconds, into the slice of the card where that window
// actually falls. A spectrum grid spans the whole view, but a mask is often a small window inside
// it: stretching its picture over the full width would show the selection across the whole card.
void drawWindowedImage(juce::Graphics& g, juce::Rectangle<float> area, double start, double end,
                       const juce::Image& image, waveview::View view)
{
    if (! image.isValid() || image.getWidth() <= 0 || image.getHeight() <= 0) return;
    if (! (end > start) || ! view.valid() || ! (view.length() > 0.0)) return;
    const double from = juce::jmax(start, view.start);
    const double to = juce::jmin(end, view.end);
    if (! (to > from)) return;
    const double span = end - start, viewLength = view.length();
    const auto width = (double) image.getWidth();
    const int sourceX = juce::jlimit(0, image.getWidth() - 1,
                                     (int) std::floor((from - start) / span * width));
    const int sourceWidth = juce::jlimit(
        1, image.getWidth() - sourceX,
        (int) std::ceil((to - start) / span * width) - sourceX);
    const float left = area.getX()
        + (float) ((from - view.start) / viewLength) * area.getWidth();
    const float right = area.getX()
        + (float) ((to - view.start) / viewLength) * area.getWidth();
    const juce::Graphics::ScopedSaveState clipped(g);
    g.reduceClipRegion(area.toNearestInt());
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImage(image, left, area.getY(), juce::jmax(1.0f, right - left), area.getHeight(),
                sourceX, 0, sourceWidth, image.getHeight());
}

float yForFrequency(const SpectrogramGrid& grid, double hz, juce::Rectangle<float> area)
{
    const double minF = grid.minFrequency, maxF = grid.maxFrequency;
    if (! (maxF > minF)) return area.getCentreY();
    const double position = grid.logFrequency && minF > 0.0
        ? std::log(juce::jlimit(minF, maxF, hz) / minF) / std::log(maxF / minF)
        : (juce::jlimit(minF, maxF, hz) - minF) / (maxF - minF);
    return area.getBottom() - (float) position * area.getHeight();
}

bool sameRange(selection::Range a, selection::Range b)
{
    return a.start == b.start && a.end == b.end;
}

// Tool cursors drawn the way graphics applications draw them: a white glyph with a dark halo, so
// it reads on both themes. Kind 0 = rectangle, 1 = lasso, 2 = magic wand.
void drawCursorStroke(juce::Graphics& g, const juce::Path& path, float thickness)
{
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.strokePath(path, juce::PathStrokeType(thickness + 1.8f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white);
    g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

void drawCursorCross(juce::Graphics& g, float cx, float cy, float arm)
{
    juce::Path cross;
    cross.startNewSubPath(cx - arm, cy); cross.lineTo(cx - 2.0f, cy);
    cross.startNewSubPath(cx + 2.0f, cy); cross.lineTo(cx + arm, cy);
    cross.startNewSubPath(cx, cy - arm); cross.lineTo(cx, cy - 2.0f);
    cross.startNewSubPath(cx, cy + 2.0f); cross.lineTo(cx, cy + arm);
    drawCursorStroke(g, cross, 1.0f);
}

void drawSparkle(juce::Graphics& g, float cx, float cy, float radius)
{
    juce::Path star;
    star.startNewSubPath(cx, cy - radius); star.lineTo(cx, cy + radius);
    star.startNewSubPath(cx - radius, cy); star.lineTo(cx + radius, cy);
    drawCursorStroke(g, star, 1.0f);
}

juce::MouseCursor spectralToolCursor(int kind)
{
    constexpr int extent = 28;
    juce::Image image(juce::Image::ARGB, extent, extent, true);
    {
        juce::Graphics g(image);
        if (kind == 0)
        {
            drawCursorCross(g, 8.0f, 8.0f, 7.0f);
            juce::Path box;
            box.addRectangle(12.0f, 13.0f, 11.0f, 8.0f);
            drawCursorStroke(g, box, 1.0f);
        }
        else if (kind == 1)
        {
            juce::Path loop;
            loop.addEllipse(6.0f, 6.0f, 15.0f, 11.0f);
            drawCursorStroke(g, loop, 1.3f);
            juce::Path rope;
            rope.startNewSubPath(8.0f, 16.0f);
            rope.cubicTo(5.0f, 23.0f, 12.0f, 26.0f, 16.0f, 21.0f);
            drawCursorStroke(g, rope, 1.3f);
        }
        else
        {
            juce::Path wand;
            wand.startNewSubPath(5.0f, 5.0f); wand.lineTo(17.0f, 21.0f);
            drawCursorStroke(g, wand, 2.2f);
            drawSparkle(g, 21.0f, 8.0f, 3.0f);
            drawSparkle(g, 25.0f, 15.0f, 2.0f);
            drawSparkle(g, 17.0f, 6.0f, 1.8f);
        }
    }
    const int hotspot = kind == 2 ? 5 : (kind == 1 ? 6 : 8);
    return juce::MouseCursor(image, hotspot, hotspot);
}
// The thumbnail cache is keyed by the hash of its input source, and JUCE's FileInputSource hashes
// the path and the modification time only. An in-place save can land inside the same millisecond
// as the previous write, so the cache then serves the audio the file had before the edit and the
// waveform keeps the old length. The size is part of the identity here, which distinguishes every
// save that changes the length and leaves the cache correct for the ordinary case too.
class IdentifiedFileSource final : public juce::InputSource
{
public:
    explicit IdentifiedFileSource(juce::File sourceFile) : file(std::move(sourceFile)) {}
    juce::InputStream* createInputStream() override { return file.createInputStream().release(); }
    juce::InputStream* createInputStreamFor(const juce::String& relatedItemPath) override
    {
        return juce::File(relatedItemPath).createInputStream().release();
    }
    juce::int64 hashCode() const override
    {
        return file.hashCode64()
            ^ file.getLastModificationTime().toMilliseconds() * 7919
            ^ file.getSize() * 104729;
    }
private:
    juce::File file;
};
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
    sourceFile = file.existsAsFile() ? file : juce::File();
    spectroFile = sourceFile;
    spectroAudio.reset();
    spectroSource = sourceFile != juce::File() ? SpectrogramService::sourceKey(sourceFile) : juce::String();
    invalidateSpectrum(true);
    if (file.existsAsFile())
    {
        thumbnail.setSource(new IdentifiedFileSource(file));
        zoomReader.reset(formats.createReaderFor(file));
        if (zoomReader != nullptr) zoomSampleRate = (int) std::lround(zoomReader->sampleRate);
    }
    else
        thumbnail.setSource(nullptr);
    refreshFromCache();
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
    // The thumbnail path cannot read these containers; PreviewPanel hands the decoded snapshot
    // to setSpectrogramAudio right after this call.
    sourceFile = {};
    spectroFile = {};
    spectroAudio.reset();
    spectroSource.clear();
    ++spectroRevision;
    invalidateSpectrum(true);
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
    // Every edit changes the PCM, so the revision moves and the spectrum is recomputed.
    setSpectrogramAudio(editedAudio, ++spectroRevision);
    repaint();
}

void WaveformDisplay::clearEditedAudio()
{
    editedAudio.reset();
    editedSlices.clear();
    columnCache.clear(); cachedWidth = 0;
    // The file is the source again; its snapshot is gone.
    spectroAudio.reset();
    spectroFile = sourceFile;
    spectroSource = sourceFile != juce::File() ? SpectrogramService::sourceKey(sourceFile) : juce::String();
    invalidateSpectrum(true);
    refreshFromCache();
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
    // The new range needs its own analysis; the debounce keeps a wheel zoom from queueing one
    // job per pixel.
    spectroDirtyAt = juce::Time::getMillisecondCounter();
    if (mode == RenderMode::spectrum) startTimer(60);
    repaint();
}

void WaveformDisplay::zoomToFit()
{
    viewRange = waveview::whole(totalLength());
    spectroDirtyAt = juce::Time::getMillisecondCounter();
    if (mode == RenderMode::spectrum) startTimer(60);
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
    // The wave is a rounded card of its own: the fill and every mark inside are clipped to the
    // same shape, and the outline closes the card against the preview panel behind it.
    const auto cardBounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto corner = (float) studio::panelRadius;
    juce::Path card;
    card.addRoundedRectangle(cardBounds, corner);
    g.setColour(theme::table);
    g.fillPath(card);

    const auto drawContent = [this, &g]
    {
        const auto total = totalLength();
        if (total <= 0)
        {
            g.setColour(theme::muted); g.drawText("Select a sample to preview",getLocalBounds(),juce::Justification::centred); return;
        }
        const auto view = effectiveView();
        const auto area = getLocalBounds().reduced(6,10).toFloat();
        if (mode == RenderMode::spectrum)
        {
            drawSpectrum(g, area, view);
        }
        else if (editedAudio != nullptr)
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
            g.setFont(fonts::font(11));
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
    };

    {
        const juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(card);
        drawContent();
    }

    g.setColour(theme::line);
    g.strokePath(card, juce::PathStrokeType(1.0f));
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
    if (mode == RenderMode::spectrum)
    {
        const int hit = spectralToolbarHit(e.x, e.y);
        if (hit >= 0)
        {
            if (hit < spectralToolbarCount)
                setSelectionTool((SelectionTool) hit);
            else
            {
                static const int tolerances[] = { 3, 6, 12, 24 };
                int index = 0;
                for (int i = 0; i < 4; ++i)
                    if (tolerances[i] == wandToleranceDb) index = i;
                wandToleranceDb = tolerances[(index + 1) % 4];
                repaint();
            }
            return;
        }
        if (tool != SelectionTool::time)
        {
            // A plain press on the selection lifts the fragment: that is how a 2D selection is
            // dragged into a DAW. Shift and Alt still draw, so the mask stays editable without
            // clearing it first, and the wand always fills, since a click is its whole gesture.
            if (tool != SelectionTool::wand && ! e.mods.isShiftDown() && ! e.mods.isAltDown()
                && pointInMask(e.position, contentArea(), effectiveView()))
            {
                externalDragTriggered = false;
                spectralDragTriggered = false;
                spectralPressInMask = true;
                dragOrigin = e.getPosition();
                return;
            }
            spectralPressInMask = false;
            beginSpectralGesture(e, contentArea(), effectiveView());
            return;
        }
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
    if (! pressWasPrimary) return;
    // Cmd/Ctrl-drag still starts the external file drag that plain dragging inside used to do.
    if (pressInsideSelection && e.mods.isCommandDown()) return;
    // Edges win over the body: a few pixels around either boundary resize the region, and that
    // never depends on the Loop chip. Only moving the whole window stays tied to the armed loop,
    // because a plain drag inside the selection is the DAW file drag.
    const auto tolerance = 5.0 * secondsPerPixel();
    if (currentSelection.valid())
    {
        if (std::abs(dragAnchor - currentSelection.start) <= tolerance) dragMode = DragMode::resizeStart;
        else if (std::abs(dragAnchor - currentSelection.end) <= tolerance) dragMode = DragMode::resizeEnd;
    }
    if (dragMode == DragMode::none && pressInsideSelection && loopArmed) dragMode = DragMode::move;
    if (dragMode != DragMode::none) { dragStartRange = currentSelection; dragStartTime = dragAnchor; }
}
void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (spectralPressInMask)
    {
        const auto delta = e.getPosition() - dragOrigin;
        if (! spectralDragTriggered && selection::exceedsDragThreshold(delta.x, delta.y))
        {
            spectralDragTriggered = true;
            if (onExternalDragRequested) onExternalDragRequested();
        }
        return;
    }
    if (spectralGesture)
    {
        updateSpectralGesture(e, contentArea(), effectiveView());
        return;
    }
    const auto total = totalLength();
    if (total <= 0) return;
    const auto delta = e.getPosition() - dragOrigin;
    // Loop mode reserves the selection body/edges for live editing. Leaving the editor is an
    // attempted external drag instead; let the preview explain why Loop must be turned off.
    auto* window = getTopLevelComponent();
    if (loopArmed && pressInsideSelection
        && ! window->getLocalBounds().contains(window->getLocalPoint(this, e.getPosition())))
    {
        if (! externalDragTriggered && selection::exceedsDragThreshold(delta.x, delta.y))
        {
            externalDragTriggered = true;
            if (onExternalDragRequested) onExternalDragRequested();
        }
        return;
    }
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
        spectralPressInMask = false;
        spectralDragTriggered = false;
        return;
    }
    if (spectralPressInMask)
    {
        // A press on the selection without a drag is a plain click: leave everything as it is.
        spectralPressInMask = false;
        spectralDragTriggered = false;
        return;
    }
    if (spectralGesture)
    {
        commitSpectralGesture(e, contentArea(), effectiveView());
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
    if (mode == RenderMode::spectrum)
    {
        if (spectralToolbarHit(e.x, e.y) >= 0)
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
        if (tool != SelectionTool::time)
        {
            refreshSpectrumCursor(pointInMask(e.position, contentArea(), effectiveView()));
            return;
        }
    }
    updateCursorFor(waveview::timeAtX(e.position.x, getWidth(), effectiveView()));
}
void WaveformDisplay::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}
bool WaveformDisplay::pointInMask(juce::Point<float> position, juce::Rectangle<float> area,
                                  waveview::View view) const
{
    if (mask == nullptr || mask->empty() || ! area.contains(position) || ! view.valid()) return false;
    const int row = mask->rowForFrequency(frequencyAtY(position.y, area, view));
    const int column = mask->columnForTime(waveview::timeAtX(position.x, getWidth(), view));
    if (row < 0 || column < 0) return false;
    // Two cells of slack: a one-row band would otherwise be impossible to grab.
    for (int r = juce::jmax(0, row - 2); r <= juce::jmin(SpectralMask::rowCount - 1, row + 2); ++r)
        for (int c = juce::jmax(0, column - 2); c <= juce::jmin(mask->columns - 1, column + 2); ++c)
            if (mask->cell(r, c)) return true;
    return false;
}
void WaveformDisplay::refreshSpectrumCursor(bool overMask)
{
    if (mode != RenderMode::spectrum || tool == SelectionTool::time)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (overMask)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    switch (tool)
    {
        case SelectionTool::rectangle:
        case SelectionTool::lasso:
        case SelectionTool::wand:
            setMouseCursor(spectralToolCursor((int) tool - 1));
            return;
        default: break;
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}
void WaveformDisplay::updateCursorFor(double time)
{
    if (! currentSelection.valid())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    // The edges are grabbable whether or not the loop is armed, so the cursor says so.
    const auto tolerance = 5.0 * secondsPerPixel();
    if (std::abs(time - currentSelection.start) <= tolerance
        || std::abs(time - currentSelection.end) <= tolerance)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (loopArmed && selection::contains(currentSelection, time))
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

void WaveformDisplay::setRenderMode(RenderMode next)
{
    if (mode == next) return;
    mode = next;
    spectralGesture = false;
    spectralPressInMask = false;
    lassoPointsHz.clear();
    if (mode == RenderMode::spectrum)
    {
        spectroDirtyAt = juce::Time::getMillisecondCounter();
        startTimer(60);
    }
    else
    {
        stopTimer();
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    repaint();
}

void WaveformDisplay::setSelectionTool(SelectionTool next)
{
    if (tool == next) return;
    tool = next;
    spectralGesture = false;
    spectralPressInMask = false;
    lassoPointsHz.clear();
    refreshSpectrumCursor(false);
    if (onSelectionToolChanged) onSelectionToolChanged();
    repaint();
}

const SpectralMask* WaveformDisplay::spectralMask() const
{
    return mask != nullptr && ! mask->empty() ? mask.get() : nullptr;
}

void WaveformDisplay::setSpectralMaskForTest(const SpectralMask& value)
{
    mask = std::make_unique<SpectralMask>(value);
    notifyMaskChanged();
}

void WaveformDisplay::clearSpectralMask()
{
    if (mask == nullptr) return;
    mask.reset();
    notifyMaskChanged();
}

void WaveformDisplay::setSpectrogramAudio(SampleAudioPtr audio, juce::uint64 revision)
{
    spectroAudio = std::move(audio);
    spectroRevision = revision;
    spectroFile = {};
    if (spectroAudio != nullptr)
        spectroSource = "snapshot:" + juce::String((juce::int64) revision) + ":"
            + juce::String((juce::int64) spectroAudio->frames) + ":"
            + juce::String(spectroAudio->sampleRate) + ":" + juce::String(spectroAudio->channels);
    else
        spectroSource.clear();
    invalidateSpectrum(true);
}

void WaveformDisplay::setSpectrumLogScale(bool logScale)
{
    if (spectroLog == logScale) return;
    spectroLog = logScale;
    // A linear chart cannot reuse the log overview; drop the detail pass and rebuild on demand.
    detailGrid.reset();
    detailImage = {};
    maskImage = {};
    spectroDirtyAt = juce::Time::getMillisecondCounter();
    if (mode == RenderMode::spectrum) startTimer(60);
    repaint();
}

bool WaveformDisplay::spectrumAvailable() const
{
    return (spectroAudio != nullptr && spectroAudio->frames > 0 && spectroAudio->sampleRate > 0)
        || spectroFile.existsAsFile();
}

double WaveformDisplay::sourceDuration() const
{
    if (spectroAudio != nullptr && spectroAudio->sampleRate > 0)
        return (double) spectroAudio->frames / (double) spectroAudio->sampleRate;
    if (zoomReader != nullptr && zoomReader->sampleRate > 0.0)
        return (double) zoomReader->lengthInSamples / zoomReader->sampleRate;
    return totalLength();
}

juce::Rectangle<float> WaveformDisplay::contentArea() const
{
    return getLocalBounds().reduced(6, 10).toFloat();
}

SpectrogramService& WaveformDisplay::spectrumService()
{
    if (spectroService == nullptr)
        spectroService = std::make_unique<SpectrogramService>(defaultSpectrumCacheFolder());
    return *spectroService;
}

void WaveformDisplay::refreshFromCache()
{
    overviewGrid.reset();
    overviewImage = {};
    detailGrid.reset();
    detailImage = {};
    spectroReadyKey.clear();
    if (! spectralCache || ! spectroLog || ! spectroFile.existsAsFile()) return;
    if (auto cached = readSpectrumCache(spectroFile))
    {
        overviewGrid = std::make_shared<const SpectrogramGrid>(std::move(cached->grid));
        overviewImage = cached->image;
        spectroReadyKey = "cache|" + spectroSource;
    }
}

void WaveformDisplay::invalidateSpectrum(bool dropResult)
{
    spectroDirtyAt = juce::Time::getMillisecondCounter();
    if (dropResult)
    {
        const bool hadMask = mask != nullptr && ! mask->empty();
        overviewGrid.reset(); overviewImage = {};
        detailGrid.reset(); detailImage = {};
        mask.reset(); maskImage = {};
        spectroReadyKey.clear();
        spectroRequestKey.clear();
        spectroError.clear();
        pendingIsOverview = false;
        if (spectroService != nullptr) spectroService->cancel();
        if (hadMask && onSpectralMaskChanged) onSpectralMaskChanged();
    }
    if (mode == RenderMode::spectrum) startTimer(60);
}

juce::String WaveformDisplay::spectrumKey(waveview::View view, juce::Rectangle<float> area,
                                          bool overview) const
{
    juce::String key = spectroSource;
    key << "|" << (overview ? "overview" : "detail")
        << "|" << juce::String((juce::int64) std::llround(view.start * 1000.0))
        << "|" << juce::String((juce::int64) std::llround(view.end * 1000.0))
        << "|" << (int) area.getWidth() << "x" << (int) area.getHeight()
        << "|" << (spectroLog ? 1 : 0);
    return key;
}

void WaveformDisplay::ensureSpectrum(waveview::View view, juce::Rectangle<float> area)
{
    if (mode != RenderMode::spectrum || ! spectrumAvailable()) return;
    if (area.getWidth() < 16.0f || area.getHeight() < 16.0f) return;
    const auto duration = sourceDuration();
    if (! (duration > 0)) return;
    const auto viewStart = juce::jmax(0.0, view.start);
    const auto viewEnd = juce::jmin(duration, view.end);
    if (! (viewEnd > viewStart)) return;

    // The cache holds a log overview. In linear mode there is no instant frame to reuse, so the
    // visible range is analysed directly (the previous behaviour).
    const bool canUseOverview = spectroLog;
    if (canUseOverview && overviewGrid == nullptr)
    {
        SpectrogramRequest request;
        request.key = spectrumKey(waveview::whole(duration), area, true);
        if (request.key == spectroRequestKey) return;
        request.startSeconds = 0.0;
        request.endSeconds = duration;
        request.params.fftSize = 2048;
        request.params.columns = 8192;
        request.params.rows = SpectralMask::rowCount;
        request.params.maxFrames = 8192;
        request.params.logFrequency = true;
        request.params.minFrequency = 20.0;
        request.params.floorDb = -90.0;
        if (spectroAudio != nullptr)
            request.audio = spectroAudio;
        else
        {
            request.file = spectroFile;
            if (spectralCache)
            {
                request.cacheSource = spectroFile;
                request.cacheOverview = true;
            }
        }
        spectrumService().prepare(std::move(request));
        spectroRequestKey = spectrumKey(waveview::whole(duration), area, true);
        pendingIsOverview = true;
        spectroError.clear();
        startTimer(60);
        return;
    }

    bool needDetail = true;
    if (canUseOverview && overviewGrid != nullptr)
    {
        const auto columnsInView = (double) overviewGrid->columns
            * (viewEnd - viewStart) / duration;
        needDetail = columnsInView < 2.0 * (double) area.getWidth();
    }
    if (! needDetail) return;

    const auto key = spectrumKey({ viewStart, viewEnd }, area, false);
    if (key == spectroRequestKey) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (spectroDirtyAt != 0 && now - spectroDirtyAt < (juce::uint32) spectrumDebounceMs) return;
    SpectrogramRequest request;
    request.key = key;
    request.startSeconds = viewStart;
    request.endSeconds = viewEnd;
    request.params.fftSize = 2048;
    request.params.columns = juce::jlimit(64, 4096, (int) area.getWidth());
    request.params.rows = juce::jlimit(32, 512, (int) area.getHeight());
    request.params.logFrequency = spectroLog;
    request.params.minFrequency = 20.0;
    request.params.floorDb = -90.0;
    if (spectroAudio != nullptr) request.audio = spectroAudio;
    else request.file = spectroFile;
    spectrumService().prepare(std::move(request));
    spectroRequestKey = key;
    pendingIsOverview = false;
    spectroError.clear();
    startTimer(60);
}

void WaveformDisplay::timerCallback()
{
    if (mode != RenderMode::spectrum)
    {
        stopTimer();
        return;
    }
    const auto state = spectrumService().state();
    if (state.key == spectroRequestKey)
    {
        if (state.phase == SpectrogramService::Phase::ready && state.grid != nullptr
            && state.key != spectroReadyKey)
        {
            if (pendingIsOverview)
            {
                overviewGrid = state.grid;
                overviewImage = spectrogramview::render(*state.grid);
            }
            else
            {
                detailGrid = state.grid;
                detailImage = spectrogramview::render(*state.grid);
            }
            spectroReadyKey = state.key;
            repaint();
        }
        else if (state.phase == SpectrogramService::Phase::failed && state.error != spectroError)
        {
            spectroError = state.error;
            repaint();
        }
    }
    ensureSpectrum(effectiveView(), contentArea());
}

const SpectrogramGrid* WaveformDisplay::displayedGrid(waveview::View view) const
{
    if (detailGrid != nullptr && gridCovers(*detailGrid, view)) return detailGrid.get();
    if (overviewGrid != nullptr) return overviewGrid.get();
    return nullptr;
}

const juce::Image* WaveformDisplay::displayedImage(waveview::View view) const
{
    if (detailGrid != nullptr && gridCovers(*detailGrid, view) && detailImage.isValid())
        return &detailImage;
    if (overviewImage.isValid()) return &overviewImage;
    return nullptr;
}

void WaveformDisplay::drawSpectrum(juce::Graphics& g, juce::Rectangle<float> area, waveview::View view)
{
    ensureSpectrum(view, area);
    const auto* grid = displayedGrid(view);
    const auto* image = displayedImage(view);
    if (spectrumAvailable() && grid != nullptr && image != nullptr && image->isValid())
    {
        drawWindowedImage(g, area, grid->startSeconds, grid->endSeconds, *image, view);
        spectrogramview::drawFrequencyLabels(g, area, *grid, spectralToolbarBounds().toFloat());
        if (grid->overview) spectrogramview::drawOverviewNote(g, area);
        drawMask(g, area, view);
        drawSpectralGesture(g, area, view);
    }
    else if (! spectrumAvailable())
    {
        g.setColour(theme::muted);
        g.setFont(fonts::font(11));
        g.drawText("Spectrum is unavailable for this source", area, juce::Justification::centred);
    }
    else if (spectroError.isNotEmpty())
    {
        g.setColour(theme::muted);
        g.setFont(fonts::font(11));
        g.drawText(spectroError, area, juce::Justification::centred);
    }
    else
    {
        g.setColour(theme::muted);
        g.setFont(fonts::font(11));
        g.drawText("Computing spectrum...", area, juce::Justification::centred);
    }
    drawSpectralToolbar(g, area);
}

void WaveformDisplay::rebuildMaskImage()
{
    maskImage = {};
    if (mask == nullptr || mask->columns <= 0) return;
    // The overlay is built on the frequency axis currently on screen, so it lands on exactly the
    // bands it covers; the mask itself stays logarithmic and absolute.
    const auto* grid = displayedGrid(effectiveView());
    const bool logScale = grid == nullptr || grid->logFrequency;
    const double minF = grid != nullptr ? grid->minFrequency : mask->minFrequency;
    const double maxF = grid != nullptr ? grid->maxFrequency : mask->maxFrequency;
    if (! (maxF > minF) || minF <= 0.0) return;
    maskImageMinHz = minF;
    maskImageMaxHz = maxF;
    maskImageLog = logScale;
    juce::Image image(juce::Image::ARGB, mask->columns, SpectralMask::rowCount, true);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    const auto colour = theme::accent.withAlpha(0.42f).getPixelARGB();
    const auto ratio = maxF / minF;
    for (int row = 0; row < SpectralMask::rowCount; ++row)
    {
        const auto position = ((double) row + 0.5) / (double) SpectralMask::rowCount;
        const double frequency = logScale ? minF * std::pow(ratio, position)
                                          : minF + (maxF - minF) * position;
        const int sourceRow = mask->rowForFrequency(frequency);
        if (sourceRow < 0) continue;
        auto* line = reinterpret_cast<juce::PixelARGB*>(
            data.getLinePointer(SpectralMask::rowCount - 1 - row));
        for (int column = 0; column < mask->columns; ++column)
            if (mask->cell(sourceRow, column)) line[column] = colour;
    }
    maskImage = std::move(image);
}

void WaveformDisplay::drawMask(juce::Graphics& g, juce::Rectangle<float> area, waveview::View view)
{
    if (mask == nullptr || mask->empty()) return;
    const auto* grid = displayedGrid(view);
    const bool logScale = grid == nullptr || grid->logFrequency;
    const double minF = grid != nullptr ? grid->minFrequency : mask->minFrequency;
    const double maxF = grid != nullptr ? grid->maxFrequency : mask->maxFrequency;
    if (! maskImage.isValid() || maskImageLog != logScale || maskImageMinHz != minF
        || maskImageMaxHz != maxF)
        rebuildMaskImage();
    if (! maskImage.isValid()) return;
    drawWindowedImage(g, area, mask->startSeconds, mask->endSeconds, maskImage, view);
    // The window's edges stay visible even when the masked cells inside it are sparse.
    g.setColour(theme::accent.withAlpha(0.6f));
    for (const auto edge : { mask->startSeconds, mask->endSeconds })
    {
        if (edge < view.start || edge > view.end) continue;
        const auto x = waveview::xAtTime(edge, getWidth(), view);
        g.drawVerticalLine((int) x, area.getY(), area.getBottom());
    }
}

void WaveformDisplay::drawSpectralGesture(juce::Graphics& g, juce::Rectangle<float> area,
                                          waveview::View view)
{
    if (! spectralGesture) return;
    const auto* grid = displayedGrid(view);
    if (grid == nullptr) return;
    if (tool == SelectionTool::rectangle)
    {
        const auto x1 = waveview::xAtTime(juce::jmin(gestureStartTime, gestureEndTime), getWidth(), view);
        const auto x2 = waveview::xAtTime(juce::jmax(gestureStartTime, gestureEndTime), getWidth(), view);
        const auto y1 = yForFrequency(*grid, juce::jmax(gestureStartHz, gestureEndHz), area);
        const auto y2 = yForFrequency(*grid, juce::jmin(gestureStartHz, gestureEndHz), area);
        const juce::Rectangle<float> bounds(x1, y1, juce::jmax(1.0f, x2 - x1),
                                            juce::jmax(1.0f, y2 - y1));
        g.setColour(theme::accent.withAlpha(0.22f));
        g.fillRect(bounds);
        g.setColour(theme::accent.withAlpha(0.9f));
        g.drawRect(bounds, 1.0f);
    }
    else if (tool == SelectionTool::lasso && lassoPointsHz.size() >= 2)
    {
        juce::Path path;
        for (size_t i = 0; i < lassoPointsHz.size(); ++i)
        {
            const auto x = waveview::xAtTime(lassoPointsHz[i].x, getWidth(), view);
            const auto y = yForFrequency(*grid, lassoPointsHz[i].y, area);
            if (i == 0) path.startNewSubPath(x, y);
            else path.lineTo(x, y);
        }
        path.closeSubPath();
        g.setColour(theme::accent.withAlpha(0.22f));
        g.fillPath(path);
        g.setColour(theme::accent.withAlpha(0.9f));
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }
}

juce::Rectangle<int> WaveformDisplay::spectralToolbarBounds() const
{
    auto area = contentArea().toNearestInt().reduced(4);
    const int width = spectralToolbarCount * toolChipWidth
        + (spectralToolbarCount - 1) * toolChipGap
        + (tool == SelectionTool::wand ? 4 + 38 : 0);
    return { area.getX(), area.getY(), width, spectralToolbarHeight };
}

int WaveformDisplay::spectralToolbarHit(int x, int y) const
{
    const auto bounds = spectralToolbarBounds();
    if (! bounds.contains(x, y)) return -1;
    for (int i = 0; i < spectralToolbarCount; ++i)
    {
        const int left = bounds.getX() + i * (toolChipWidth + toolChipGap);
        if (x >= left && x < left + toolChipWidth) return i;
    }
    if (tool == SelectionTool::wand && x >= bounds.getRight() - 38)
        return spectralToolbarCount;
    return -1;
}

void WaveformDisplay::drawSpectralToolbar(juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::ignoreUnused(area);
    const auto bounds = spectralToolbarBounds().toFloat();
    g.setColour(theme::panel.withAlpha(0.86f));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setFont(fonts::font(10));
    for (int i = 0; i < spectralToolbarCount; ++i)
    {
        const int left = spectralToolbarBounds().getX() + i * (toolChipWidth + toolChipGap);
        const juce::Rectangle<float> chip((float) left, bounds.getY(),
                                          (float) toolChipWidth, bounds.getHeight());
        const bool active = (int) tool == i;
        if (active)
        {
            g.setColour(theme::accent.withAlpha(0.9f));
            g.fillRoundedRectangle(chip.reduced(1.0f), 3.0f);
            g.setColour(theme::panel);
        }
        else
        {
            g.setColour(theme::muted);
        }
        g.drawText(toolLabel(i), chip, juce::Justification::centred);
    }
    if (tool == SelectionTool::wand)
    {
        const juce::Rectangle<float> chip(bounds.getRight() - 38.0f, bounds.getY(), 36.0f,
                                          bounds.getHeight());
        g.setColour(theme::raised);
        g.fillRoundedRectangle(chip.reduced(1.0f), 3.0f);
        g.setColour(theme::text);
        g.drawText(juce::String(wandToleranceDb) + " dB", chip, juce::Justification::centred);
    }
}

double WaveformDisplay::frequencyAtY(float y, juce::Rectangle<float> area, waveview::View view) const
{
    const auto* grid = displayedGrid(view);
    const double minF = grid != nullptr ? grid->minFrequency : 20.0;
    const double maxF = grid != nullptr ? grid->maxFrequency : nyquist();
    const bool logScale = grid != nullptr ? grid->logFrequency : true;
    const double position = juce::jlimit(0.0, 1.0,
        (double) (area.getBottom() - y) / (double) juce::jmax(1.0f, area.getHeight()));
    return logScale && minF > 0.0 ? minF * std::pow(maxF / minF, position)
                                  : minF + (maxF - minF) * position;
}

double WaveformDisplay::nyquist() const
{
    if (spectroAudio != nullptr && spectroAudio->sampleRate > 0)
        return spectroAudio->sampleRate * 0.5;
    if (zoomReader != nullptr && zoomReader->sampleRate > 0.0)
        return zoomReader->sampleRate * 0.5;
    const auto* grid = displayedGrid(effectiveView());
    return grid != nullptr ? grid->maxFrequency : 20000.0;
}

void WaveformDisplay::beginSpectralGesture(const juce::MouseEvent& e, juce::Rectangle<float> area,
                                           waveview::View view)
{
    if (! area.contains(e.position)) return;
    if (displayedGrid(view) == nullptr) return;
    const double time = waveview::timeAtX(e.position.x, getWidth(), view);
    const double hz = frequencyAtY(e.position.y, area, view);
    if (tool == SelectionTool::wand)
    {
        runWand(e, area, view);
        return;
    }
    spectralGesture = true;
    gestureStartTime = gestureEndTime = time;
    gestureStartHz = gestureEndHz = hz;
    lassoPointsHz.clear();
    if (tool == SelectionTool::lasso) lassoPointsHz.emplace_back(time, hz);
    repaint();
}

void WaveformDisplay::updateSpectralGesture(const juce::MouseEvent& e, juce::Rectangle<float> area,
                                            waveview::View view)
{
    if (! spectralGesture) return;
    const double time = waveview::timeAtX(e.position.x, getWidth(), view);
    const double hz = frequencyAtY(e.position.y, area, view);
    gestureEndTime = time;
    gestureEndHz = hz;
    if (tool == SelectionTool::lasso)
    {
        if (lassoPointsHz.size() < 2000) lassoPointsHz.emplace_back(time, hz);
    }
    repaint();
}

void WaveformDisplay::commitSpectralGesture(const juce::MouseEvent& e, juce::Rectangle<float> area,
                                            waveview::View view)
{
    juce::ignoreUnused(area, view);
    if (! spectralGesture) return;
    spectralGesture = false;
    const bool subtract = e.mods.isAltDown();
    const bool replace = ! e.mods.isShiftDown() && ! subtract;
    if (tool == SelectionTool::rectangle)
    {
        const auto start = juce::jmin(gestureStartTime, gestureEndTime);
        const auto end = juce::jmax(gestureStartTime, gestureEndTime);
        if (end - start > 0.0)
        {
            commitMaskWindow(start, end, replace);
            mask->addRectangle(start, end, juce::jmin(gestureStartHz, gestureEndHz),
                               juce::jmax(gestureStartHz, gestureEndHz), subtract);
        }
    }
    else if (tool == SelectionTool::lasso && lassoPointsHz.size() >= 3)
    {
        double start = lassoPointsHz.front().x, end = start;
        for (const auto& point : lassoPointsHz)
        {
            start = juce::jmin(start, point.x);
            end = juce::jmax(end, point.x);
        }
        if (end - start > 0.0)
        {
            commitMaskWindow(start, end, replace);
            mask->addPolygon(lassoPointsHz, subtract);
        }
    }
    lassoPointsHz.clear();
    notifyMaskChanged();
}

void WaveformDisplay::commitMaskWindow(double start, double end, bool replace)
{
    if (! (end > start)) return;
    // A small margin keeps a mask drawn at the window edge from losing its first/last cells.
    const auto pad = juce::jmax(0.001, (end - start) * 0.001);
    if (mask == nullptr) mask = std::make_unique<SpectralMask>();
    const auto nyq = nyquist();
    if (replace || mask->columns == 0 || mask->empty())
    {
        mask->clear();
        mask->setWindow(start - pad, end + pad, nyq);
    }
    else
    {
        mask->setWindow(juce::jmin(mask->startSeconds, start - pad),
                        juce::jmax(mask->endSeconds, end + pad), nyq);
    }
}

void WaveformDisplay::runWand(const juce::MouseEvent& e, juce::Rectangle<float> area,
                              waveview::View view)
{
    const auto* grid = displayedGrid(view);
    if (grid == nullptr || ! grid->valid() || ! area.contains(e.position)) return;
    const auto time = waveview::timeAtX(e.position.x, getWidth(), view);
    const auto hz = frequencyAtY(e.position.y, area, view);
    const auto column = juce::jlimit(0, grid->columns - 1,
        (int) std::floor((time - grid->startSeconds) / (grid->endSeconds - grid->startSeconds)
                         * (double) grid->columns));
    const auto position = juce::jlimit(0.0, 1.0,
        grid->logFrequency && grid->minFrequency > 0.0
            ? std::log(hz / grid->minFrequency) / std::log(grid->maxFrequency / grid->minFrequency)
            : (hz - grid->minFrequency) / (grid->maxFrequency - grid->minFrequency));
    const auto row = juce::jlimit(0, grid->rows - 1, (int) std::floor(position * (double) grid->rows));
    const auto base = grid->at(row, column);
    const auto tolerance = (double) wandToleranceDb;
    std::vector<uint8_t> filled((size_t) grid->rows * (size_t) grid->columns, 0);
    std::vector<int> stack;
    stack.push_back(row * grid->columns + column);
    filled[(size_t) stack.back()] = 1;
    int visited = 1;
    constexpr int maxCells = 300000;
    while (! stack.empty() && visited < maxCells)
    {
        const int index = stack.back();
        stack.pop_back();
        const int r = index / grid->columns, c = index % grid->columns;
        const int neighbours[4][2] = { { r - 1, c }, { r + 1, c }, { r, c - 1 }, { r, c + 1 } };
        for (const auto& neighbour : neighbours)
        {
            if (neighbour[0] < 0 || neighbour[0] >= grid->rows || neighbour[1] < 0
                || neighbour[1] >= grid->columns)
                continue;
            const int next = neighbour[0] * grid->columns + neighbour[1];
            if (filled[(size_t) next] != 0) continue;
            if (std::abs((double) grid->at(neighbour[0], neighbour[1]) - (double) base) > tolerance)
                continue;
            filled[(size_t) next] = 1;
            stack.push_back(next);
            ++visited;
        }
    }

    double start = time, end = time;
    for (int r = 0; r < grid->rows; ++r)
        for (int c = 0; c < grid->columns; ++c)
        {
            if (filled[(size_t) (r * grid->columns + c)] == 0) continue;
            const auto cellTime = grid->startSeconds
                + ((double) c + 0.5) / (double) grid->columns
                    * (grid->endSeconds - grid->startSeconds);
            start = juce::jmin(start, cellTime);
            end = juce::jmax(end, cellTime);
        }
    const bool subtract = e.mods.isAltDown();
    const bool replace = ! e.mods.isShiftDown() && ! subtract;
    commitMaskWindow(start, end, replace);
    mask->addRaster(*grid, filled, subtract);
    notifyMaskChanged();
}

void WaveformDisplay::notifyMaskChanged()
{
    maskImage = {};
    repaint();
    if (onSpectralMaskChanged) onSpectralMaskChanged();
}
} // namespace sonicator
