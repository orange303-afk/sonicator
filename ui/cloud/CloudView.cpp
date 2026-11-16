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
constexpr float pointSize = 4.0f;
constexpr float margin = 28.0f;
constexpr double minScale = 0.05;
constexpr double maxScale = 40.0;
constexpr size_t livePointThreshold = 1500; // above this, the composed frame is cached and blitted
constexpr float lensRadius = 120.0f; // about twice the old area
constexpr float lensPull = 1.05f;
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
    index.build(current);
    visibleValid = false;
    frameValid = false;
    rebuildColours();
    rebuildLayers();
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

void CloudView::setFilter(std::vector<bool>)
{
    // The map is the whole tagged library. The list's search/folder/tag filter belongs to the
    // list; applying it here left a handful of bright points on a dark field, which read as a
    // broken map. All points keep the same brightness.
    if (matches.empty()) return;
    matches.clear();
    frameValid = false;
    repaint();
    if (isShowing()) updateTimerRate();
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
    return index.hitTest(world.x, world.y, radius);
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
    for (const auto& [colour, indices] : byColour)
    {
        juce::Path path;
        for (auto i : indices)
        {
            const auto& point = current.points[i];
            const auto x = (point.x - current.bounds.getX()) * layerScale;
            const auto y = (point.y - current.bounds.getY()) * layerScale;
            path.addEllipse(x - size * 0.5f, y - size * 0.5f, size, size);
        }
        if (path.isEmpty()) continue;
        g.setColour(juce::Colour(colour));
        g.fillPath(path);
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
    std::map<uint32_t, juce::Path> paths;
    struct LensPoint
    {
        juce::Point<float> position;
        float size = pointSize;
        juce::Colour colour;
    };
    std::vector<LensPoint> lensPoints;

    for (auto i : visible)
    {
        if (isPointVisible(i) != matching) continue;
        const auto& point = current.points[i];
        auto screen = worldToScreen(point.x, point.y) + sway;
        if (withDrift && motion)
            screen += juce::Point<float>(pointDrift(i, false), pointDrift(i, true));
        const auto colour = juce::Colour(point.colour).withAlpha(matching ? 1.0f : 0.30f);
        if (useLens && lensStrength > 0.002f)
        {
            const auto delta = screen - lensPos;
            const auto mask = lensMask(delta.getDistanceFromOrigin());
            if (mask > 0.001f)
            {
                auto displaced = screen + delta * (lensPull * lensStrength * mask);
                lensPoints.push_back({ displaced, pointSize * (1.0f + 0.5f * lensStrength * mask), colour });
                continue;
            }
        }
        paths[point.colour].addEllipse(screen.x - pointSize * 0.5f, screen.y - pointSize * 0.5f,
                                       pointSize, pointSize);
    }
    for (const auto& [colour, path] : paths)
    {
        g.setColour(juce::Colour(colour).withAlpha(matching ? 1.0f : 0.30f));
        g.fillPath(path);
    }
    for (const auto& entry : lensPoints)
    {
        g.setColour(entry.colour);
        g.fillEllipse(entry.position.x - entry.size * 0.5f, entry.position.y - entry.size * 0.5f,
                      entry.size, entry.size);
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

void CloudView::applyLensMask(juce::Image& patch, juce::Point<float> centre) const
{
    juce::Image::BitmapData data(patch, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < patch.getHeight(); ++y)
        for (int x = 0; x < patch.getWidth(); ++x)
        {
            const auto dx = (float) x + 0.5f - centre.x;
            const auto dy = (float) y + 0.5f - centre.y;
            const auto distance = std::sqrt(dx * dx + dy * dy);
            float alpha = 0.0f;
            if (distance < lensRadius)
            {
                // Full strength well inside, a long smooth fade to nothing at the rim.
                // A long, very soft rim: the patch fades over 80 % of the radius, so the lens
                // reads as a blur of the neighbourhood, not as a circle laid on the map.
                const auto t = juce::jlimit(0.0f, 1.0f, (1.0f - distance / lensRadius) / 0.80f);
                alpha = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            }
            if (alpha >= 0.999f) continue;
            auto* pixel = reinterpret_cast<juce::PixelARGB*>(data.getPixelPointer(x, y));
            pixel->multiplyAlpha((int) (alpha * 255.0f));
        }
}

void CloudView::drawLens(juce::Graphics& g, const std::vector<size_t>& visible, juce::Point<float> sway)
{
    if (lensStrength <= 0.01f || ! hasLensTarget) return;
    const auto centre = lensPos + sway;
    const auto live = visible.size() <= livePointThreshold || (float) viewScale > layerScale * 1.25f;
    if (! live)
    {
        const int radius = (int) std::ceil(lensRadius);
        auto rect = juce::Rectangle<int>((int) std::floor(centre.x) - radius, (int) std::floor(centre.y) - radius,
                                         radius * 2 + 1, radius * 2 + 1)
                        .getIntersection(getLocalBounds());
        if (rect.isEmpty()) return;
        lensPatch = juce::Image(juce::Image::ARGB, rect.getWidth(), rect.getHeight(), true);
        juce::Graphics pg(lensPatch);
        pg.fillAll(theme::background);
        const auto lensCentreLocal = centre - juce::Point<float>((float) rect.getX(), (float) rect.getY());
        const bool filtered = ! matches.empty();
        for (auto i : visible)
        {
            const auto& point = current.points[i];
            auto screen = worldToScreen(point.x, point.y) + sway;

            const auto delta = screen - centre;
            const auto distance = delta.getDistanceFromOrigin();
            if (distance >= lensRadius) continue;
            const auto mask = lensMask(distance);
            const auto displaced = screen + delta * (lensPull * lensStrength * mask);
            const auto size = pointSize * (1.0f + 0.5f * lensStrength * mask);
            const auto visiblePoint = isPointVisible(i);
            auto colour = juce::Colour(point.colour);
            colour = colour.withAlpha(filtered && ! visiblePoint ? 0.30f : 1.0f);
            pg.setColour(colour);
            pg.fillEllipse(displaced.x - (float) rect.getX() - size * 0.5f,
                           displaced.y - (float) rect.getY() - size * 0.5f, size, size);
        }
        applyLensMask(lensPatch, lensCentreLocal);
        g.setOpacity(introAlpha());
        g.drawImageAt(lensPatch, rect.getX(), rect.getY());
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
    if (live)
    {
        // Close up, the points are drawn live: they can drift with the ambient motion and the lens
        // can displace them without a patch.
        g.setOpacity(intro);
        drawPointsLive(g, visible, true, sway, true, true);
        g.setOpacity(1.0f);
    }
    else
    {
        // The whole map is one cached frame. Ambient motion and the lens only blit over it.
        ensureFrameCache(visible);
        g.setOpacity(intro);
        g.drawImageAt(frameCache, juce::roundToInt(sway.x), juce::roundToInt(sway.y));
        g.setOpacity(1.0f);
    }
    drawLabels(g, visible, sway);
    drawLens(g, visible, sway);
    paintDynamic(g, sway);
}

void CloudView::paintDynamic(juce::Graphics& g, juce::Point<float> sway)
{
    if (selectedIndex != CloudIndex::none && selectedSample != 0 && selectedIndex < current.points.size())
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

    if (hovered != CloudIndex::none && hovered < current.points.size())
    {
        const auto& point = current.points[hovered];
        auto screen = worldToScreen(point.x, point.y) + sway;
        screen += lensOffsetForScreen(screen);
        // Caption above the point, kept inside the panel.
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
    const bool fast = animating || hasLensTarget || lensStrength > 0.01f;
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
    // A dimmed point is still a real file: the filter says it does not match, not that it cannot
    // be inspected. Clicking it must always open it.
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
