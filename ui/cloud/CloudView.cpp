#include "CloudView.h"
#include "ui/shell/Theme.h"
#include "core/export/ExternalFileDrag.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace sonicator::cloud
{
namespace
{
constexpr float pointSize = 2.4f;
constexpr float margin = 28.0f;
constexpr double minScale = 0.05;
constexpr double maxScale = 40.0;
constexpr size_t livePointThreshold = 1500; // above this, the composed frame is cached and blitted
constexpr float lensRadius = 220.0f; // several times the old area
constexpr float lensPull = 0.65f;
constexpr float lensFeather = 0.7f;
constexpr float introDurationMs = 650.0f;
constexpr float swayAmplitude = 1.0f;
constexpr float driftAmplitude = 0.6f;
constexpr float driftHz = 0.14f;
constexpr int fieldMaxDimension = 256;

uint32_t hashBits(size_t index)
{
    auto x = (uint64_t) index + 0x9e3779b97f4a7c15ULL;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (uint32_t) (x & 0xffffffu);
}
} // namespace

CloudView::CloudView()
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    addAndMakeVisible(backButton);
    addAndMakeVisible(fitButton);
    backButton.setEnabled(false);
    backButton.onClick = [this] { cameraBack(); };
    fitButton.onClick = [this]
    {
        pushHistory();
        animateTo(cameraForFit());
    };
}

CloudView::~CloudView() = default;

void CloudView::setLayout(CloudLayout layout)
{
    const auto sameFingerprint = current.valid() && layout.fingerprint == current.fingerprint
        && layout.points.size() == current.points.size();
    current = std::move(layout);
    // A mask belongs to one set of points; the owner recomputes it for the new layout.
    matches.clear();
    index.build(current);
    visibleValid = false;
    frameValid = false;
    rebuildColours();
    rebuildLayers();
    rebuildIslandCounts();
    hovered = CloudIndex::none;
    selectedIndex = CloudIndex::none;
    for (size_t i = 0; i < current.points.size(); ++i)
        if (current.points[i].sampleId == selectedSample) { selectedIndex = i; break; }
    if (! sameFingerprint)
    {
        history.clear();
        viewScale = 1.0;
        viewPan = {};
        introElapsedMs = 0.0f;
        labelAlpha = 0.0f;
        animateTo(cameraForFit());
        backButton.setEnabled(false);
    }
    if (isShowing()) updateTimerRate();
    repaint();
}

void CloudView::setFilter(std::vector<bool> mask)
{
    // The mask is parallel to layout().points; an empty or mismatched mask means "everything
    // matches". The layout itself never changes: the camera, the positions and the cached map stay,
    // so switching a filter cannot reflow the view.
    if (mask.size() != current.points.size()) mask.clear();
    if (mask == matches) return;
    matches = std::move(mask);
    frameValid = false;
    visibleValid = false;
    rebuildIslandCounts();
    // A hidden point cannot keep the hover: its caption and its audition would describe a file the
    // map no longer shows.
    if (hovered != CloudIndex::none && ! isPointVisible(hovered)) setHovered(CloudIndex::none);
    repaint();
    if (isShowing()) updateTimerRate();
}

void CloudView::rebuildIslandCounts()
{
    islandVisibleCounts.assign(current.islands.size(), 0);
    if (matches.empty())
    {
        for (size_t i = 0; i < current.islands.size(); ++i) islandVisibleCounts[i] = current.islands[i].count;
        return;
    }
    for (size_t i = 0; i < current.points.size() && i < matches.size(); ++i)
    {
        if (! matches[i]) continue;
        const auto island = current.points[i].islandIndex;
        if (island >= 0 && (size_t) island < islandVisibleCounts.size()) ++islandVisibleCounts[(size_t) island];
    }
}

void CloudView::setLabel(int64_t sampleId, juce::String text)
{
    labels[sampleId] = std::move(text);
}

void CloudView::setSelectedSample(int64_t sampleId)
{
    if (selectedSample == sampleId) return;
    selectedSample = sampleId;
    selectedIndex = CloudIndex::none;
    for (size_t i = 0; i < current.points.size(); ++i)
        if (current.points[i].sampleId == sampleId) { selectedIndex = i; break; }
    repaint();
}

void CloudView::setLoading(bool isLoading, const juce::String& message)
{
    if (loading == isLoading && loadingText == message) return;
    loading = isLoading;
    loadingText = message;
    repaint();
}

void CloudView::setEmptyMessage(const juce::String& message)
{
    emptyText = message;
    repaint();
}

void CloudView::setNotice(const juce::String& text)
{
    if (notice == text) return;
    notice = text;
    repaint();
}

bool CloudView::isPointVisible(size_t index) const
{
    if (index >= current.points.size()) return false;
    if (matches.empty()) return true;
    return index < matches.size() && matches[index];
}

juce::Point<float> CloudView::worldToScreen(float x, float y) const
{
    return { (float) (x * viewScale + viewPan.x), (float) (y * viewScale + viewPan.y) };
}

juce::Point<float> CloudView::screenToWorld(juce::Point<float> screen) const
{
    if (viewScale <= 0) return {};
    return { (float) ((screen.x - viewPan.x) / viewScale), (float) ((screen.y - viewPan.y) / viewScale) };
}

CloudView::Camera CloudView::cameraForFit() const
{
    Camera camera;
    if (! current.valid() || getWidth() <= 0 || getHeight() <= 0) return camera;
    const auto usableWidth = juce::jmax(1.0f, (float) getWidth() - 2 * margin);
    const auto usableHeight = juce::jmax(1.0f, (float) getHeight() - 2 * margin);
    const auto bounds = current.bounds;
    camera.scale = juce::jmin(usableWidth / juce::jmax(1.0f, bounds.getWidth()),
                              usableHeight / juce::jmax(1.0f, bounds.getHeight()));
    camera.scale = juce::jlimit(minScale, maxScale, camera.scale);
    const auto centre = bounds.getCentre();
    camera.pan = { getWidth() / 2.0 - centre.x * camera.scale, getHeight() / 2.0 - centre.y * camera.scale };
    return camera;
}

void CloudView::fitAll()
{
    animateTo(cameraForFit());
}

void CloudView::lookAndFeelChanged()
{
    // Tag label colours follow the theme; the shaped labels carry the old colour, so rebuild.
    rebuildLabels();
    frameValid = false;
    repaint();
}

void CloudView::visibilityChanged()
{
    if (isShowing())
    {
        lastTimerMs = juce::Time::getMillisecondCounter();
        updateTimerRate();
    }
    else
    {
        stopTimer();
        currentTimerHz = 0;
        lastTimerMs = 0;
    }
}

void CloudView::parentHierarchyChanged()
{
    if (isShowing())
    {
        if (! isTimerRunning()) lastTimerMs = juce::Time::getMillisecondCounter();
        updateTimerRate();
    }
    else
    {
        stopTimer();
        currentTimerHz = 0;
    }
}

void CloudView::resized()
{
    backButton.setBounds(getWidth() - 190, 10, 80, 26);
    fitButton.setBounds(getWidth() - 102, 10, 92, 26);
    frameValid = false;
    frameValid = false;
    if (! current.valid()) return;
    if (lastWidth == 0 || lastHeight == 0)
    {
        animateTo(cameraForFit());
    }
    else
    {
        // A panel resize keeps the centre and the zoom: the map is the user's place, not a
        // picture that re-fits itself whenever a splitter moves.
        viewPan.x += (getWidth() - lastWidth) * 0.5;
        viewPan.y += (getHeight() - lastHeight) * 0.5;
        target = Camera { viewScale, viewPan };
        repaint();
    }
    lastWidth = juce::jmax(0, getWidth());
    lastHeight = juce::jmax(0, getHeight());
}

void CloudView::applyCamera(const Camera& camera)
{
    viewScale = juce::jlimit(minScale, maxScale, camera.scale);
    viewPan = camera.pan;
}

void CloudView::animateTo(Camera camera)
{
    camera.scale = juce::jlimit(minScale, maxScale, camera.scale);
    lastMotionMs = juce::Time::getMillisecondCounter();
    target = camera;
    animating = true;
    updateTimerRate();
}

void CloudView::pushHistory()
{
    const Camera now { viewScale, viewPan };
    if (! history.empty() && history.back() == now) return;
    history.push_back(now);
    if (history.size() > 20) history.erase(history.begin());
    backButton.setEnabled(true);
}

void CloudView::cameraBack()
{
    if (history.empty()) return;
    const auto previous = history.back();
    history.pop_back();
    backButton.setEnabled(! history.empty());
    animateTo(previous);
}

void CloudView::zoomBy(float factor, juce::Point<float> screenPoint)
{
    if (! current.valid()) return;
    pushHistory();
    Camera camera { viewScale, viewPan };
    const auto world = screenToWorld(screenPoint);
    camera.scale = juce::jlimit(minScale, maxScale, camera.scale * (double) factor);
    camera.pan = { (double) screenPoint.x - world.x * camera.scale, (double) screenPoint.y - world.y * camera.scale };
    animateTo(camera);
}

void CloudView::focusOnSample(int64_t sampleId)
{
    if (! current.valid() || getWidth() <= 0) return;
    for (size_t i = 0; i < current.points.size(); ++i)
    {
        if (current.points[i].sampleId != sampleId) continue;
        pushHistory();
        const auto world = juce::Point<float>(current.points[i].x, current.points[i].y);
        Camera camera { viewScale, viewPan };
        // A small step, not a jump: the map keeps its context, the point lands near the centre.
        const auto desiredScale = juce::jmax(viewScale, 1.6);
        const auto screen = worldToScreen(world.x, world.y);
        const auto targetScreen = juce::Point<float>(getWidth() / 2.0f, getHeight() / 2.0f);
        const auto blend = 0.35f;
        camera.scale = desiredScale;
        camera.pan = { (double) (viewPan.x + (targetScreen.x - screen.x) * blend),
                       (double) (viewPan.y + (targetScreen.y - screen.y) * blend) };
        animateTo(camera);
        return;
    }
}

size_t CloudView::pointAtScreen(juce::Point<int> screen) const
{
    if (! current.valid()) return CloudIndex::none;
    const auto world = screenToWorld({ (float) screen.x, (float) screen.y });
    const auto radius = juce::jmax(6.0f, 8.0f / (float) viewScale);
    const auto hit = index.hitTest(world.x, world.y, radius);
    if (hit == CloudIndex::none || isPointVisible(hit)) return hit;
    // A hidden point under the cursor must not swallow a click meant for a visible neighbour: take
    // the nearest visible point inside the same disc.
    size_t best = CloudIndex::none;
    float bestDistance = radius * radius;
    for (auto candidate : index.within({ world.x - radius, world.y - radius, radius * 2.0f, radius * 2.0f }))
    {
        if (! isPointVisible(candidate)) continue;
        const auto& point = current.points[candidate];
        const auto dx = point.x - world.x;
        const auto dy = point.y - world.y;
        const auto distance = dx * dx + dy * dy;
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

void CloudView::rebuildColours()
{
    byColour.clear();
    for (size_t i = 0; i < current.points.size(); ++i)
        byColour[current.points[i].colour].push_back(i);
}

void CloudView::rebuildLayers()
{
    rebuildPointLayer();
    rebuildLabels();
}

void CloudView::rebuildPointLayer()
{
    pointLayer = {};
    if (! current.valid() || current.bounds.isEmpty()) return;
    // Build the point layer at the fit scale, so at the overview a point is exactly pointSize on
    // screen. If the layout arrives before the panel is laid out the fit is meaningless; resized()
    // rebuilds the layer once the real size is known.
    layerScale = (getWidth() > 0 && getHeight() > 0)
        ? juce::jlimit(0.35f, 2.0f, (float) cameraForFit().scale)
        : 1.0f;
    const auto width = juce::jmax(1, (int) std::ceil(current.bounds.getWidth() * layerScale));
    const auto height = juce::jmax(1, (int) std::ceil(current.bounds.getHeight() * layerScale));
    pointLayer = juce::Image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(pointLayer);
    const auto size = juce::jmax(1.6f, pointSize);
    // One fill per dot, not one Path per colour. A Path holding thousands of ellipses makes the
    // graphics backend walk the whole outline for every scanline: on the owner's 52k library a
    // single 19k-dot path held the message thread for tens of seconds. Direct fills of the same
    // dots measured 183 ms in Debug, and the colours stay exact.
    for (const auto& [colour, indices] : byColour)
    {
        g.setColour(juce::Colour(colour));
        for (auto i : indices)
        {
            const auto& point = current.points[i];
            const auto x = (point.x - current.bounds.getX()) * layerScale;
            const auto y = (point.y - current.bounds.getY()) * layerScale;
            if (x < -size || y < -size || x > width + size || y > height + size) continue;
            g.fillEllipse(x - size * 0.5f, y - size * 0.5f, size, size);
        }
    }
}

void CloudView::rebuildLabels()
{
    islandLabels.clear();
    islandLabelWidths.clear();
    islandLabelImages.clear();
    labelOrder.clear();
    if (! current.valid()) return;
    islandLabels.reserve(current.islands.size());
    islandLabelWidths.reserve(current.islands.size());
    islandLabelImages.reserve(current.islands.size());
    for (const auto& island : current.islands)
    {
        const auto text = island.name + " (" + juce::String(island.count) + ")";
        const auto width = juce::GlyphArrangement::getStringWidthInt(studio::captionFont(), text) + 8;
        islandLabels.push_back(text);
        islandLabelWidths.push_back(width);
        constexpr int scale = 2; // shaped at 2x so the label stays crisp on a Retina display
        juce::Image label(juce::Image::ARGB, width * scale, 14 * scale, true);
        juce::Graphics g(label);
        g.addTransform(juce::AffineTransform::scale((float) scale));
        g.setColour(theme::tagText(juce::Colour(island.colour)));
        g.setFont(studio::captionFont());
        g.drawText(text, 0, 0, width, 14, juce::Justification::centred);
        islandLabelImages.push_back(std::move(label));
        labelOrder.push_back(labelOrder.size());
    }
    std::sort(labelOrder.begin(), labelOrder.end(), [this](size_t a, size_t b)
    {
        return current.islands[a].radius > current.islands[b].radius;
    });
}

void CloudView::drawLayer(juce::Graphics& g, const juce::Image& layer, juce::Point<float> sway) const
{
    if (! layer.isValid() || ! current.valid()) return;
    const auto factor = (float) viewScale / layerScale;
    const auto transform = juce::AffineTransform::scale(factor).translated(
        current.bounds.getX() * (float) viewScale + (float) viewPan.x + sway.x,
        current.bounds.getY() * (float) viewScale + (float) viewPan.y + sway.y);
    g.drawImageTransformed(layer, transform);
}

juce::Point<float> CloudView::ambientSway() const
{
    if (! motion) return {};
    const auto t = (float) motionMs / 1000.0f;
    constexpr auto twoPi = juce::MathConstants<float>::twoPi;
    return { std::sin(t * twoPi * 0.060f) * swayAmplitude,
             std::cos(t * twoPi * 0.047f + 1.7f) * swayAmplitude * 0.7f };
}

float CloudView::pointDrift(size_t index, bool vertical) const
{
    if (! motion) return 0.0f;
    const auto phase = (float) hashBits(index) / 16777215.0f * juce::MathConstants<float>::twoPi;
    const auto t = (float) motionMs / 1000.0f;
    const auto wobble = std::sin(t * juce::MathConstants<float>::twoPi * driftHz + (vertical ? phase * 1.3f : phase));
    return wobble * driftAmplitude;
}

float CloudView::lensMask(float distance) const
{
    if (distance >= lensRadius) return 0.0f;
    const auto x = distance / lensRadius;
    const auto falloff = juce::jmax(0.0f, 1.0f - x * x);
    return std::pow(falloff, 1.0f + 3.0f * lensFeather);
}

juce::Point<float> CloudView::lensOffsetForScreen(juce::Point<float> screen) const
{
    if (lensStrength <= 0.002f) return {};
    const auto delta = screen - lensPos;
    const auto distance = delta.getDistanceFromOrigin();
    const auto mask = lensMask(distance);
    if (mask <= 0.0f || distance < 1e-3f) return {};
    return delta * (lensPull * lensStrength * mask);
}

juce::Point<float> CloudView::lensOffsetForTest(size_t index) const
{
    if (index >= current.points.size()) return {};
    const auto& point = current.points[index];
    return lensOffsetForScreen(worldToScreen(point.x, point.y));
}

void CloudView::drawPointsLive(juce::Graphics& g, const std::vector<size_t>& visible, bool matching,
                               juce::Point<float> sway, bool useLens, bool withDrift) const
{
    // Dots are filled one by one. Batching them into a Path looks cheaper, but the path geometry
    // with thousands of ellipses is what stalled the map; a direct fill measured ten times faster
    // and keeps the antialiased edge and the exact blended colour.
    const auto alpha = matching ? 1.0f : 0.30f;
    for (auto i : visible)
    {
        if (isPointVisible(i) != matching) continue;
        const auto& point = current.points[i];
        auto screen = worldToScreen(point.x, point.y) + sway;
        if (withDrift && motion)
            screen += juce::Point<float>(pointDrift(i, false), pointDrift(i, true));
        auto size = pointSize;
        if (useLens && lensStrength > 0.002f)
        {
            const auto delta = screen - lensPos;
            const auto mask = lensMask(delta.getDistanceFromOrigin());
            if (mask > 0.001f)
            {
                screen += delta * (lensPull * lensStrength * mask);
                size = pointSize * (1.0f + 0.5f * lensStrength * mask);
            }
        }
        g.setColour(juce::Colour(point.colour).withAlpha(alpha));
        g.fillEllipse(screen.x - size * 0.5f, screen.y - size * 0.5f, size, size);
    }
}

void CloudView::drawLabels(juce::Graphics& g, const std::vector<size_t>& visible, juce::Point<float> sway)
{
    if (labelAlpha <= 0.01f || islandLabelImages.empty()) return;
    const bool crowded = visible.size() > livePointThreshold;
    std::vector<juce::Rectangle<int>> placed;
    int shown = 0;
    g.setFont(studio::captionFont());
    const auto opacity = labelAlpha * introAlpha();
    for (auto islandIndex : labelOrder)
    {
        if (crowded && shown >= 10) break;
        const auto& island = current.islands[islandIndex];
        const auto centre = worldToScreen(island.x, island.y) + sway;
        const auto radius = island.radius * (float) viewScale;
        if (radius <= 18.0f) continue;
        if (centre.x + radius < 0 || centre.y + radius < 0
            || centre.x - radius > getWidth() || centre.y - radius > getHeight())
            continue;
        if (islandIndex >= islandLabels.size()) continue;
        if ((size_t) islandIndex < islandVisibleCounts.size() && islandVisibleCounts[(size_t) islandIndex] == 0)
            continue; // the filter hides every file in this island
        const auto width = islandLabelWidths[islandIndex];
        const juce::Rectangle<int> box((int) (centre.x - width / 2), (int) (centre.y - radius - 16), width, 14);
        bool collides = false;
        for (const auto& other : placed)
            if (other.intersects(box)) { collides = true; break; }
        if (collides) continue;
        placed.push_back(box);
        ++shown;
        g.setOpacity(opacity);
        g.drawImageTransformed(islandLabelImages[islandIndex],
                               juce::AffineTransform::scale(0.5f).translated((float) box.getX(), (float) box.getY()));
        g.setOpacity(1.0f);
    }
}

void CloudView::ensureFrameCache(const std::vector<size_t>& visible)
{
    const Camera camera { viewScale, viewPan };
    const auto width = juce::jmax(1, getWidth());
    const auto height = juce::jmax(1, getHeight());
    if (frameValid && frameCamera == camera
        && frameCache.getWidth() == width && frameCache.getHeight() == height)
        return;
    frameCache = juce::Image(juce::Image::RGB, width, height, true);
    juce::Graphics g(frameCache);
    g.fillAll(theme::background);
    if (! matches.empty())
    {
        // A filter hides the other points; it does not dim them. Everything on the map keeps the
        // same brightness.
        drawPointsLive(g, visible, true, {}, false, false);
    }
    else if (visible.size() <= livePointThreshold)
    {
        drawPointsLive(g, visible, true, {}, false, false);
    }
    else
    {
        drawLayer(g, pointLayer, {});
    }
    frameCamera = camera;
    frameValid = true;
}

void CloudView::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    if (! current.valid())
    {
        g.setColour(theme::muted);
        g.setFont(studio::controlFont());
        g.drawText(loading ? (loadingText.isEmpty() ? juce::String("Building the map...") : loadingText) : emptyText,
                   0, 0, getWidth(), getHeight(), juce::Justification::centred);
        return;
    }
    const auto area = worldViewport().expanded(4.0f / (float) viewScale);
    if (! visibleValid || area != visibleArea)
    {
        visibleCache = index.within(area);
        visibleArea = area;
        visibleValid = true;
    }
    const auto& visible = visibleCache;
    const auto sway = ambientSway();
    const auto intro = introAlpha();
    const bool filtered = ! matches.empty();
    const bool live = visible.size() <= livePointThreshold || (float) viewScale > layerScale * 1.25f;
    const bool lensActive = lensStrength > 0.002f;
    if (live)
    {
        // Close up, the points are drawn live: they can drift with the ambient motion and the lens
        // can displace them without a patch.
        g.setOpacity(intro);
        drawPointsLive(g, visible, true, sway, true, true);
        g.setOpacity(1.0f);
    }
    else if (lensActive && intro > 0.999f)
    {
        // Hovering the overview used to redraw all 52k points every frame, at up to 60 Hz: the map
        // crawled and the window stopped answering. The frame stays the cached blit; only the
        // lens disc is cleared and its own points are drawn live on top. Displacement at the rim
        // is a fraction of a pixel, so the live dot covers the cached one it replaces.
        ensureFrameCache(visible);
        g.drawImageAt(frameCache, juce::roundToInt(sway.x), juce::roundToInt(sway.y));
        const auto radius = lensRadius + 1.0f;
        g.setColour(theme::background);
        g.fillEllipse(lensPos.x - radius, lensPos.y - radius, radius * 2.0f, radius * 2.0f);
        // The spatial index narrows the walk to the files around the cursor instead of scanning
        // every visible point every frame.
        const auto worldRadius = radius / (float) juce::jmax(0.01, viewScale) + 2.0f;
        const auto worldCentre = screenToWorld(lensPos - sway);
        const auto nearby = index.within({ worldCentre.x - worldRadius, worldCentre.y - worldRadius,
                                           worldRadius * 2.0f, worldRadius * 2.0f });
        visibleScratch.clear();
        for (auto i : nearby)
        {
            const auto& point = current.points[i];
            if ((worldToScreen(point.x, point.y) + sway).getDistanceFrom(lensPos) > radius) continue;
            visibleScratch.push_back(i);
        }
        drawPointsLive(g, visibleScratch, true, sway, true, true);
    }
    else
    {
        // The whole map is one cached frame. Ambient motion only blits over it.
        ensureFrameCache(visible);
        g.setOpacity(intro);
        g.drawImageAt(frameCache, juce::roundToInt(sway.x), juce::roundToInt(sway.y));
        g.setOpacity(1.0f);
    }
    drawLabels(g, visible, sway);
    paintDynamic(g, sway);
}

void CloudView::paintDynamic(juce::Graphics& g, juce::Point<float> sway)
{
    if (selectedIndex != CloudIndex::none && selectedSample != 0 && selectedIndex < current.points.size()
        && isPointVisible(selectedIndex))
    {
        const auto& point = current.points[selectedIndex];
        auto screen = worldToScreen(point.x, point.y) + sway;
        screen += lensOffsetForScreen(screen);
        // Glow belongs to the selection only.
        juce::ColourGradient halo(theme::accent.withAlpha(0.30f), screen.x, screen.y,
                                  theme::accent.withAlpha(0.0f), screen.x + 16.0f, screen.y, true);
        g.setGradientFill(halo);
        g.fillEllipse(screen.x - 16, screen.y - 16, 32, 32);
        g.setColour(theme::accent);
        g.drawEllipse(screen.x - 5, screen.y - 5, 10, 10, 1.4f);
    }

    if (hovered != CloudIndex::none && hovered < current.points.size() && isPointVisible(hovered))
    {
        const auto& point = current.points[hovered];
        auto screen = worldToScreen(point.x, point.y) + sway;
        screen += lensOffsetForScreen(screen);
        // Caption above the point, kept inside the panel.
        // A small, soft halo around the point under the cursor. The big glow stays reserved for
        // the selected sample.
        const auto colour = juce::Colour(point.colour);
        juce::ColourGradient halo(colour.withAlpha(0.38f * hoverGlow), screen.x, screen.y,
                                  colour.withAlpha(0.0f), screen.x + 12.0f, screen.y, true);
        g.setGradientFill(halo);
        g.fillEllipse(screen.x - 12.0f, screen.y - 12.0f, 24.0f, 24.0f);

        auto found = labels.find(point.sampleId);
        const auto text = found != labels.end() ? found->second : "#" + juce::String(point.sampleId);
        g.setFont(studio::captionFont());
        const auto width = juce::GlyphArrangement::getStringWidthInt(studio::captionFont(), text) + 12;
        auto x = juce::jlimit(4.0f, (float) juce::jmax(4, getWidth() - width - 4), screen.x - width / 2);
        auto y = juce::jmax(4.0f, screen.y - 30.0f);
        g.setColour(theme::raised);
        g.fillRoundedRectangle(x, y, (float) width, 18.0f, 4.0f);
        g.setColour(theme::text);
        g.drawText(text, (int) x, (int) y, width, 18, juce::Justification::centred);
    }

    if (notice.isNotEmpty())
    {
        g.setFont(studio::controlFont());
        const auto width = juce::GlyphArrangement::getStringWidthInt(studio::controlFont(), notice) + 24;
        const juce::Rectangle<int> box(getWidth() / 2 - width / 2, 16, width, 24);
        g.setColour(theme::raised.withAlpha(0.92f));
        g.fillRoundedRectangle(box.toFloat(), 6.0f);
        g.setColour(theme::text);
        g.drawText(notice, box, juce::Justification::centred);
    }

    if (loading)
    {
        g.setColour(theme::muted);
        g.setFont(studio::captionFont());
        g.drawText(loadingText.isEmpty() ? juce::String("Building the map...") : loadingText,
                   0, getHeight() - 26, getWidth() - 190, 20, juce::Justification::centredRight);
    }
}

juce::Rectangle<float> CloudView::worldViewport() const
{
    const auto topLeft = screenToWorld({ 0, 0 });
    const auto bottomRight = screenToWorld({ (float) getWidth(), (float) getHeight() });
    return { topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y };
}

float CloudView::introAlpha() const
{
    if (! motion) return 1.0f;
    const auto t = juce::jlimit(0.0f, 1.0f, (float) (introElapsedMs / (double) introDurationMs));
    return 1.0f - std::pow(1.0f - t, 3.0f); // ease-out cubic
}

void CloudView::advance(double milliseconds)
{
    const auto step = (float) juce::jlimit(1.0, 60.0, milliseconds);
    if (motion) motionMs += step;
    if (introElapsedMs < introDurationMs) introElapsedMs = juce::jmin((double) introDurationMs, introElapsedMs + step);

    if (animating)
    {
        const auto factor = 1.0 - std::pow(0.75, (double) step / 16.6667);
        Camera next { viewScale, viewPan };
        next.scale += (target.scale - next.scale) * factor;
        next.pan.x += (target.pan.x - next.pan.x) * factor;
        next.pan.y += (target.pan.y - next.pan.y) * factor;
        applyCamera(next);
        const auto close = std::abs(target.scale - next.scale) < 0.002
            && target.pan.getDistanceFrom(next.pan) < 0.15;
        if (close)
        {
            applyCamera(target);
            animating = false;
        }
    }

    const auto lensWanted = hasLensTarget ? 1.0f : 0.0f;
    const auto lensStep = 1.0f - std::pow(0.82f, step / 16.6667f);
    if (std::abs(lensStrength - lensWanted) > 0.002f) lensStrength += (lensWanted - lensStrength) * lensStep;
    else lensStrength = lensWanted;
    if (hasLensTarget) lensPos += (lensTarget - lensPos) * lensStep;
    if (lensStrength < 0.002f) lensStrength = 0.0f;

    const auto glowTarget = hovered == CloudIndex::none ? 0.0f : 1.0f;
    if (std::abs(hoverGlow - glowTarget) > 0.01f) hoverGlow += (glowTarget - hoverGlow) * 0.25f;
    else hoverGlow = glowTarget;

    const auto settled = juce::Time::getMillisecondCounter() - lastMotionMs > 120;
    const auto targetLabel = settled ? 1.0f : 0.0f;
    if (std::abs(labelAlpha - targetLabel) > 0.01f) labelAlpha += (targetLabel - labelAlpha) * 0.2f;
    else labelAlpha = targetLabel;

    repaint();
    updateTimerRate();
}

void CloudView::updateTimerRate()
{
    if (! isShowing())
    {
        stopTimer();
        currentTimerHz = 0;
        return;
    }
    const bool fast = animating || hasLensTarget || lensStrength > 0.01f
        || std::abs(hoverGlow - (hovered == CloudIndex::none ? 0.0f : 1.0f)) > 0.01f;
    const auto wanted = fast ? 60 : 30;
    if (wanted != currentTimerHz)
    {
        startTimerHz(wanted);
        currentTimerHz = wanted;
    }
}

void CloudView::timerCallback()
{
    if (! isShowing())
    {
        stopTimer();
        currentTimerHz = 0;
        return;
    }
    const auto now = juce::Time::getMillisecondCounter();
    const auto step = lastTimerMs == 0 ? 16.0 : (double) (now - lastTimerMs);
    lastTimerMs = now;
    advance(step);
}

void CloudView::setHovered(size_t newHovered)
{
    if (newHovered != CloudIndex::none && ! isPointVisible(newHovered)) newHovered = CloudIndex::none;
    if (newHovered == hovered) return;
    hovered = newHovered;
    if (hovered != CloudIndex::none)
    {
        if (onHover) onHover(current.points[hovered].sampleId);
    }
    else if (onHoverEnd) onHoverEnd();
    if (isShowing()) updateTimerRate();
    repaint();
}

void CloudView::mouseMove(const juce::MouseEvent& event)
{
    if (dragging) return;
    lastMousePosition = event.getPosition();
    lensTarget = event.position;
    hasLensTarget = true;
    setHovered(pointAtScreen(event.getPosition()));
    if (isShowing()) updateTimerRate();
}

void CloudView::mouseExit(const juce::MouseEvent&)
{
    hasLensTarget = false;
    setHovered(CloudIndex::none);
    if (isShowing()) updateTimerRate();
}

void CloudView::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        const auto found = pointAtScreen(event.getPosition());
        if (found != CloudIndex::none && isPointVisible(found) && onContextMenu)
            onContextMenu(current.points[found].sampleId, event.getScreenPosition());
        return;
    }
    dragging = false;
    draggedOut = false;
    pressedPoint = pointAtScreen(event.getPosition());
    if (pressedPoint != CloudIndex::none && ! isPointVisible(pressedPoint)) pressedPoint = CloudIndex::none;
    dragStart = event.position;
    dragOrigin = viewPan;
    grabKeyboardFocus();
}

void CloudView::mouseDrag(const juce::MouseEvent& event)
{
    if (! current.valid()) return;
    if (! dragging && ! draggedOut && pressedPoint != CloudIndex::none
        && ExternalFileDrag::exceedsThreshold((int) (event.position.x - dragStart.x),
                                              (int) (event.position.y - dragStart.y)))
    {
        // A press on a point drags the file; a press on the background drags the map.
        const auto id = current.points[pressedPoint].sampleId;
        pressedPoint = CloudIndex::none;
        setHovered(CloudIndex::none);
        hasLensTarget = false;
        if (onDragOut) draggedOut = onDragOut(id, this);
        if (draggedOut) return;
    }
    if (! dragging && event.position.getDistanceFrom(dragStart) < 3.0f) return;
    if (! dragging)
    {
        dragging = true;
        pushHistory();
        setHovered(CloudIndex::none);
        hasLensTarget = false;
    }
    viewPan = dragOrigin + juce::Point<double>(event.position.x - dragStart.x, event.position.y - dragStart.y);
    lastMotionMs = juce::Time::getMillisecondCounter();
    repaint();
}

void CloudView::activate(size_t found)
{
    if (found == CloudIndex::none || found >= current.points.size()) return;
    // A hidden point is not on the map: the filter says the file is excluded right now, so a press
    // over the empty field behind it must not open it.
    if (! isPointVisible(found)) return;
    if (onActivate) onActivate(current.points[found].sampleId);
}

void CloudView::mouseUp(const juce::MouseEvent& event)
{
    if (draggedOut) { draggedOut = false; dragging = false; pressedPoint = CloudIndex::none; return; }
    pressedPoint = CloudIndex::none;
    if (dragging) { dragging = false; return; }
    lensTarget = event.position;
    hasLensTarget = true;
    activate(pointAtScreen(event.getPosition()));
    if (isShowing()) updateTimerRate();
}

void CloudView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto found = pointAtScreen(event.getPosition());
    if (found == CloudIndex::none) return;
    const auto& point = current.points[found];
    if (point.islandIndex < 0 || (size_t) point.islandIndex >= current.islands.size()) return;
    const auto& island = current.islands[(size_t) point.islandIndex];
    pushHistory();
    Camera camera;
    const auto fit = juce::jmin(((float) getWidth() - 2 * margin) / juce::jmax(1.0f, island.radius * 2),
                                ((float) getHeight() - 2 * margin) / juce::jmax(1.0f, island.radius * 2));
    camera.scale = juce::jlimit(minScale, maxScale, (double) fit);
    camera.pan = { getWidth() / 2.0 - island.x * camera.scale, getHeight() / 2.0 - island.y * camera.scale };
    animateTo(camera);
}

void CloudView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (! current.valid()) return;
    lensTarget = event.position;
    hasLensTarget = true;
    const auto factor = std::pow(1.25f, wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 3.0f);
    zoomBy(factor, event.position);
    if (isShowing()) updateTimerRate();
}

void CloudView::hoverForTest(juce::Point<int> screen)
{
    lensTarget = screen.toFloat();
    hasLensTarget = true;
    setHovered(pointAtScreen(screen));
}
} // namespace sonicator::cloud
