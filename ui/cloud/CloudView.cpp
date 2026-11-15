#include "CloudView.h"
#include "ui/shell/Theme.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace sonicator::cloud
{
namespace
{
constexpr float pointSize = 2.6f;
constexpr float margin = 28.0f;
constexpr double minScale = 0.05;
constexpr double maxScale = 40.0;
}

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
    rebuildColours();
    rebuildLayers();
    hovered = CloudIndex::none;
    if (! sameFingerprint)
    {
        history.clear();
        viewScale = 1.0;
        viewPan = {};
        animateTo(cameraForFit());
        backButton.setEnabled(false);
    }
    repaint();
}

void CloudView::setFilter(std::vector<bool> newMatches)
{
    matches = std::move(newMatches);
    repaint();
}

void CloudView::setLabel(int64_t sampleId, juce::String text)
{
    labels[sampleId] = std::move(text);
}

void CloudView::setSelectedSample(int64_t sampleId)
{
    if (selectedSample == sampleId) return;
    selectedSample = sampleId;
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

void CloudView::resized()
{
    backButton.setBounds(getWidth() - 190, 10, 80, 26);
    fitButton.setBounds(getWidth() - 102, 10, 92, 26);
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
    target = camera;
    animating = true;
    startTimerHz(60);
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
    islandLayer = {};
    pointLayer = {};
    if (! current.valid() || current.bounds.isEmpty()) return;
    constexpr float maxPixels = 1400.0f;
    layerScale = juce::jlimit(0.05f, 2.0f,
                              juce::jmin(maxPixels / current.bounds.getWidth(), maxPixels / current.bounds.getHeight()));
    const auto width = juce::jmax(1, (int) std::ceil(current.bounds.getWidth() * layerScale));
    const auto height = juce::jmax(1, (int) std::ceil(current.bounds.getHeight() * layerScale));
    auto toLayer = [this](float x, float y)
    {
        return juce::Point<float>((x - current.bounds.getX()) * layerScale, (y - current.bounds.getY()) * layerScale);
    };

    islandLabels.clear();
    islandLabelWidths.clear();
    islandLabels.reserve(current.islands.size());
    islandLabelWidths.reserve(current.islands.size());
    for (const auto& island : current.islands)
    {
        const auto text = island.name + " (" + juce::String(island.count) + ")";
        islandLabels.push_back(text);
        islandLabelWidths.push_back(juce::GlyphArrangement::getStringWidthInt(studio::captionFont(), text) + 8);
    }

    islandLayer = juce::Image(juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g(islandLayer);
        for (const auto& island : current.islands)
        {
            const auto centre = toLayer(island.x, island.y);
            const auto radius = island.radius * layerScale;
            const auto colour = juce::Colour(island.colour);
            g.setColour(colour.withAlpha(theme::isDark ? 0.07f : 0.06f));
            g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
            g.setColour(colour.withAlpha(theme::isDark ? 0.28f : 0.33f));
            g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2, 1.0f);
        }
    }

    pointLayer = juce::Image(juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g(pointLayer);
        const auto size = juce::jmax(1.6f, pointSize);
        for (const auto& [colour, indices] : byColour)
        {
            juce::Path path;
            for (auto i : indices)
            {
                const auto& point = current.points[i];
                const auto position = toLayer(point.x, point.y);
                path.addRectangle(position.x - size * 0.5f, position.y - size * 0.5f, size, size);
            }
            if (path.isEmpty()) continue;
            g.setColour(juce::Colour(colour));
            g.fillPath(path);
        }
    }
}

void CloudView::drawLayer(juce::Graphics& g, const juce::Image& layer) const
{
    if (! layer.isValid() || ! current.valid()) return;
    const auto factor = (float) viewScale / layerScale;
    const auto transform = juce::AffineTransform::scale(factor).translated(
        current.bounds.getX() * (float) viewScale + (float) viewPan.x,
        current.bounds.getY() * (float) viewScale + (float) viewPan.y);
    g.drawImageTransformed(layer, transform);
}

void CloudView::drawPointsLive(juce::Graphics& g, const std::vector<size_t>& visible, bool matching) const
{
    std::map<uint32_t, std::vector<size_t>> buckets;
    for (auto i : visible)
        if (isPointVisible(i) == matching) buckets[current.points[i].colour].push_back(i);
    for (const auto& [colour, indices] : buckets)
    {
        juce::Path path;
        for (auto i : indices)
        {
            const auto screen = worldToScreen(current.points[i].x, current.points[i].y);
            path.addRectangle(screen.x - pointSize * 0.5f, screen.y - pointSize * 0.5f, pointSize, pointSize);
        }
        if (path.isEmpty()) continue;
        g.setColour(juce::Colour(colour).withAlpha(matching ? 1.0f : 0.12f));
        g.fillPath(path);
    }
}

juce::Rectangle<float> CloudView::worldViewport() const
{
    const auto topLeft = screenToWorld({ 0, 0 });
    const auto bottomRight = screenToWorld({ (float) getWidth(), (float) getHeight() });
    return { topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y };
}

void CloudView::timerCallback()
{
    bool needsRepaint = false;
    if (animating)
    {
        Camera next { viewScale, viewPan };
        const auto zoomStep = (target.scale - next.scale) * 0.25;
        next.scale += zoomStep;
        next.pan.x += (target.pan.x - next.pan.x) * 0.25;
        next.pan.y += (target.pan.y - next.pan.y) * 0.25;
        applyCamera(next);
        const auto close = std::abs(target.scale - next.scale) < 0.002 && target.pan.getDistanceFrom(next.pan) < 0.15;
        if (close)
        {
            applyCamera(target);
            animating = false;
        }
        needsRepaint = true;
    }
    const auto wantedGlow = hovered == CloudIndex::none ? 0.0f : 1.0f;
    if (std::abs(glow - wantedGlow) > 0.02f)
    {
        glow += (wantedGlow - glow) * 0.25f;
        needsRepaint = true;
    }
    else glow = wantedGlow;
    if (needsRepaint) repaint();
    if (! animating && glow == wantedGlow) stopTimer();
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
    startTimerHz(60);
    repaint();
}

void CloudView::drawPoint(juce::Graphics&, size_t, juce::Point<float>, float) {}


void CloudView::drawHover(juce::Graphics& g)
{
    if (hovered == CloudIndex::none) return;
    const auto& point = current.points[hovered];
    const auto screen = worldToScreen(point.x, point.y);
    const auto colour = juce::Colour(point.colour);
    g.setColour(colour.withAlpha(0.18f * glow));
    g.fillEllipse(screen.x - 14, screen.y - 14, 28, 28);
    g.setColour(colour.withAlpha(0.35f * glow));
    g.fillEllipse(screen.x - 8, screen.y - 8, 16, 16);
    g.setColour(colour);
    g.fillEllipse(screen.x - 4, screen.y - 4, 8, 8);
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

    // Islands and points are static world-space layers; the camera only changes how they are
    // blitted. Labels are drawn live so they stay crisp and keep their size at every zoom.
    drawLayer(g, islandLayer);
    // Labels are placed largest island first and skipped when they would collide: a crowded middle
    // then shows the big regions rather than a pile of unreadable text. Zooming in brings the rest.
    std::vector<size_t> labelOrder(current.islands.size());
    std::iota(labelOrder.begin(), labelOrder.end(), (size_t) 0);
    std::sort(labelOrder.begin(), labelOrder.end(), [this](size_t a, size_t b)
    {
        return current.islands[a].radius > current.islands[b].radius;
    });
    std::vector<juce::Rectangle<int>> placed;
    for (auto islandIndex : labelOrder)
    {
        const auto& island = current.islands[islandIndex];
        const auto centre = worldToScreen(island.x, island.y);
        const auto radius = island.radius * (float) viewScale;
        if (radius <= 18.0f) continue;
        if (centre.x + radius < 0 || centre.y + radius < 0 || centre.x - radius > getWidth() || centre.y - radius > getHeight())
            continue;
        if (islandIndex >= islandLabels.size()) continue;
        const auto width = islandLabelWidths[islandIndex];
        const juce::Rectangle<int> box((int) (centre.x - width / 2), (int) (centre.y - radius - 16), width, 14);
        bool collides = false;
        for (const auto& other : placed)
            if (other.intersects(box)) { collides = true; break; }
        if (collides) continue;
        placed.push_back(box);
        g.setColour(theme::tagText(juce::Colour(island.colour)));
        g.setFont(studio::captionFont());
        g.drawText(islandLabels[islandIndex], box, juce::Justification::centred);
    }

    const auto visible = index.within(worldViewport().expanded(4.0f / (float) viewScale));
    const bool filtered = ! matches.empty();
    if (filtered)
    {
        // The whole map stays as context; only the matching files are drawn at full strength.
        g.setOpacity(0.12f);
        drawLayer(g, pointLayer);
        g.setOpacity(1.0f);
        drawPointsLive(g, visible, true);
    }
    else if (visible.size() > 8000)
    {
        // Zoomed out, the cached layer is one blit; zoomed in, only a small part is on screen and
        // the points are drawn live and crisp.
        drawLayer(g, pointLayer);
    }
    else
    {
        drawPointsLive(g, visible, true);
    }

    if (selectedSample != 0)
        for (const auto& point : current.points)
            if (point.sampleId == selectedSample)
            {
                const auto screen = worldToScreen(point.x, point.y);
                g.setColour(theme::accent);
                g.drawEllipse(screen.x - 5, screen.y - 5, 10, 10, 1.4f);
                break;
            }

    drawHover(g);

    if (hovered != CloudIndex::none)
    {
        // Caption above the point, kept inside the panel.
        const auto& point = current.points[hovered];
        const auto screen = worldToScreen(point.x, point.y);
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

    if (loading)
    {
        g.setColour(theme::muted);
        g.setFont(studio::captionFont());
        g.drawText(loadingText.isEmpty() ? juce::String("Building the map...") : loadingText,
                   0, getHeight() - 26, getWidth() - 190, 20, juce::Justification::centredRight);
    }
}

void CloudView::mouseMove(const juce::MouseEvent& event)
{
    if (dragging) return;
    setHovered(pointAtScreen(event.getPosition()));
}

void CloudView::mouseExit(const juce::MouseEvent&)
{
    setHovered(CloudIndex::none);
}

void CloudView::mouseDown(const juce::MouseEvent& event)
{
    dragging = false;
    dragStart = event.position;
    dragOrigin = viewPan;
    grabKeyboardFocus();
}

void CloudView::mouseDrag(const juce::MouseEvent& event)
{
    if (! current.valid()) return;
    if (! dragging && event.position.getDistanceFrom(dragStart) < 3.0f) return;
    if (! dragging)
    {
        dragging = true;
        pushHistory();
        setHovered(CloudIndex::none);
    }
    viewPan = dragOrigin + juce::Point<double>(event.position.x - dragStart.x, event.position.y - dragStart.y);
    repaint();
}

void CloudView::activate(size_t found)
{
    if (found == CloudIndex::none || found >= current.points.size()) return;
    if (! isPointVisible(found)) return; // dimmed by a filter: inspect, do not change the selection
    if (onActivate) onActivate(current.points[found].sampleId);
}

void CloudView::mouseUp(const juce::MouseEvent& event)
{
    if (dragging) { dragging = false; return; }
    activate(pointAtScreen(event.getPosition()));
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
    const auto factor = std::pow(1.25f, wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 3.0f);
    zoomBy(factor, event.position);
}
}
