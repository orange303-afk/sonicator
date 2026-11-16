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
constexpr float pointSize = 2.6f;
constexpr float margin = 28.0f;
constexpr double minScale = 0.05;
constexpr double maxScale = 40.0;
constexpr size_t livePointThreshold = 1500; // above this, the composed frame is cached and blitted
constexpr float lensRadius = 84.0f;
constexpr float lensPull = 1.05f;
constexpr float lensFeather = 0.7f;
constexpr float fieldDarkOpacity = 0.21f;
constexpr float fieldLightOpacity = 0.16f;
constexpr float introDurationMs = 650.0f;
constexpr float breatheAmplitude = 0.05f;
constexpr float breatheHz = 0.09f;
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

void CloudView::setFilter(std::vector<bool> newMatches)
{
    matches = std::move(newMatches);
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
    rebuildField();
    rebuildLabels();
}

void CloudView::rebuildPointLayer()
{
    pointLayer = {};
    if (! current.valid() || current.bounds.isEmpty()) return;
    constexpr float maxPixels = 1024.0f;
    layerScale = juce::jlimit(0.05f, 2.0f,
                              juce::jmin(maxPixels / current.bounds.getWidth(), maxPixels / current.bounds.getHeight()));
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
            path.addRectangle(x - size * 0.5f, y - size * 0.5f, size, size);
        }
        if (path.isEmpty()) continue;
        g.setColour(juce::Colour(colour));
        g.fillPath(path);
    }
}

void CloudView::rebuildField()
{
    fieldImage = {};
    if (! current.valid() || current.islands.empty() || current.bounds.isEmpty()) return;
    const auto bounds = current.bounds;
    fieldBounds = bounds;
    fieldScale = juce::jlimit(0.02f, 2.0f,
                              juce::jmin((float) fieldMaxDimension / bounds.getWidth(),
                                         (float) fieldMaxDimension / bounds.getHeight()));
    const auto width = juce::jmax(1, (int) std::ceil(bounds.getWidth() * fieldScale));
    const auto height = juce::jmax(1, (int) std::ceil(bounds.getHeight() * fieldScale));
    fieldImage = juce::Image(juce::Image::ARGB, width, height, true);

    struct Node
    {
        float x = 0, y = 0, sigma = 1, weight = 0, r = 0, g = 0, b = 0;
    };
    std::vector<Node> nodes;
    nodes.reserve(current.islands.size());
    for (const auto& island : current.islands)
    {
        Node node;
        node.x = island.x;
        node.y = island.y;
        node.sigma = juce::jmax(1.0f, island.radius * 0.95f);
        node.weight = std::pow((double) juce::jmax(1, island.count), 0.35);
        const auto colour = juce::Colour(island.colour);
        node.r = colour.getFloatRed();
        node.g = colour.getFloatGreen();
        node.b = colour.getFloatBlue();
        nodes.push_back(node);
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto worldX = bounds.getX() + ((float) x + 0.5f) / fieldScale;
            const auto worldY = bounds.getY() + ((float) y + 0.5f) / fieldScale;
            float sr = 0, sg = 0, sb = 0, sw = 0;
            for (const auto& node : nodes)
            {
                const auto dx = worldX - node.x, dy = worldY - node.y;
                const auto w = (float) (node.weight
                    * std::exp(-(dx * dx + dy * dy) / (2.0f * node.sigma * node.sigma)));
                sr += node.r * w;
                sg += node.g * w;
                sb += node.b * w;
                sw += w;
            }
            if (sw <= 1e-5f) continue;
            const auto alpha = juce::jlimit(0.0f, 1.0f, 1.0f - std::exp(-sw / 14.0f));
            fieldImage.setPixelAt(x, y, juce::Colour::fromFloatRGBA(sr / sw, sg / sw, sb / sw, alpha));
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

void CloudView::drawField(juce::Graphics& g, juce::Point<float> sway, float opacity) const
{
    if (! fieldImage.isValid() || opacity <= 0.001f) return;
    const auto factor = (float) viewScale / fieldScale;
    const auto transform = juce::AffineTransform::scale(factor).translated(
        fieldBounds.getX() * (float) viewScale + (float) viewPan.x + sway.x,
        fieldBounds.getY() * (float) viewScale + (float) viewPan.y + sway.y);
    g.setOpacity(juce::jlimit(0.0f, 1.0f, opacity));
    g.drawImageTransformed(fieldImage, transform);
    g.setOpacity(1.0f);
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
        const auto colour = juce::Colour(point.colour).withAlpha(matching ? 1.0f : 0.12f);
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
        paths[point.colour].addRectangle(screen.x - pointSize * 0.5f, screen.y - pointSize * 0.5f,
                                         pointSize, pointSize);
    }
    for (const auto& [colour, path] : paths)
    {
        g.setColour(juce::Colour(colour).withAlpha(matching ? 1.0f : 0.12f));
        g.fillPath(path);
    }
    for (const auto& entry : lensPoints)
    {
        g.setColour(entry.colour);
        g.fillRect(entry.position.x - entry.size * 0.5f, entry.position.y - entry.size * 0.5f,
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
                const auto t = juce::jlimit(0.0f, 1.0f, (1.0f - distance / lensRadius) / 0.45f);
                alpha = t * t * (3.0f - 2.0f * t);
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
    const auto live = visible.size() <= livePointThreshold;
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
        // The field under the patch, in the same place the screen shows it.
        if (fieldImage.isValid())
        {
            const auto factor = (float) viewScale / fieldScale;
            const auto transform = juce::AffineTransform::scale(factor).translated(
                fieldBounds.getX() * (float) viewScale + (float) viewPan.x + sway.x - rect.getX(),
                fieldBounds.getY() * (float) viewScale + (float) viewPan.y + sway.y - rect.getY());
            pg.setOpacity(fieldDarkOpacity);
            pg.drawImageTransformed(fieldImage, transform);
            pg.setOpacity(1.0f);
        }
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
            colour = colour.withAlpha(filtered && ! visiblePoint ? 0.12f : 1.0f);
            pg.setColour(colour);
            pg.fillRect(displaced.x - (float) rect.getX() - size * 0.5f,
                        displaced.y - (float) rect.getY() - size * 0.5f, size, size);
        }
        applyLensMask(lensPatch, lensCentreLocal);
        g.setOpacity(introAlpha());
        g.drawImageAt(lensPatch, rect.getX(), rect.getY());
        g.setOpacity(1.0f);
    }
    // A very faint tint at the lens centre: it reads as depth, not as an island disc.
    if (hovered != CloudIndex::none && hovered < current.points.size())
    {
        const auto colour = juce::Colour(current.points[hovered].colour);
        const auto alpha = 0.05f * lensStrength;
        juce::ColourGradient gradient(colour.withAlpha(alpha), centre.x, centre.y,
                                      colour.withAlpha(0.0f), centre.x + lensRadius, centre.y, true);
        g.setGradientFill(gradient);
        g.fillEllipse(centre.x - lensRadius, centre.y - lensRadius, lensRadius * 2, lensRadius * 2);
    }
}

void CloudView::ensureFrameCache(const std::vector<size_t>& visible)
{
    const Camera camera { viewScale, viewPan };
    const auto width = juce::jmax(1, getWidth());
    const auto height = juce::jmax(1, getHeight());
    if (frameValid && frameCamera == camera && std::abs(frameFilterBlend - filterBlend) < 0.003f
        && frameCache.getWidth() == width && frameCache.getHeight() == height)
        return;
    frameCache = juce::Image(juce::Image::RGB, width, height, true);
    juce::Graphics g(frameCache);
    g.fillAll(theme::background);
    const auto baseField = theme::isDark ? fieldDarkOpacity : fieldLightOpacity;
    drawField(g, {}, baseField * 0.95f);
    if (! matches.empty())
    {
        // The whole map stays as context; only the matching files are drawn at full strength.
        g.setOpacity(1.0f - 0.88f * filterBlend);
        drawLayer(g, pointLayer, {});
        g.setOpacity(1.0f);
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
    frameFilterBlend = filterBlend;
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
    const bool live = visible.size() <= livePointThreshold;
    const auto baseField = theme::isDark ? fieldDarkOpacity : fieldLightOpacity;
    const auto breathe01 = motion
        ? 0.5f + 0.5f * std::sin(motionMs / 1000.0f * juce::MathConstants<float>::twoPi * breatheHz)
        : 0.5f;

    if (live)
    {
        // Close up, the points are drawn live: they can drift with the ambient motion and the lens
        // can displace them without a patch.
        drawField(g, sway, intro * baseField * (0.95f + 0.05f * breathe01));
        if (filtered)
        {
            g.setOpacity(intro * (1.0f - 0.88f * filterBlend));
            drawPointsLive(g, visible, false, sway, true, true);
            g.setOpacity(intro);
            drawPointsLive(g, visible, true, sway, true, true);
        }
        else
        {
            g.setOpacity(intro);
            drawPointsLive(g, visible, true, sway, true, true);
        }
        g.setOpacity(1.0f);
    }
    else
    {
        // The whole map is one cached frame. Ambient motion and the lens only blit over it.
        ensureFrameCache(visible);
        g.setOpacity(intro);
        g.drawImageAt(frameCache, juce::roundToInt(sway.x), juce::roundToInt(sway.y));
        g.setOpacity(1.0f);
        drawField(g, sway, intro * baseField * 0.05f * breathe01);
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
        g.setColour(theme::accent);
        g.drawEllipse(screen.x - 5, screen.y - 5, 10, 10, 1.4f);
    }

    if (hovered != CloudIndex::none && hovered < current.points.size())
    {
        const auto& point = current.points[hovered];
        auto screen = worldToScreen(point.x, point.y) + sway;
        screen += lensOffsetForScreen(screen);
        const auto colour = juce::Colour(point.colour);
        g.setColour(colour.withAlpha(0.18f * glow));
        g.fillEllipse(screen.x - 14, screen.y - 14, 28, 28);
        g.setColour(colour.withAlpha(0.35f * glow));
        g.fillEllipse(screen.x - 8, screen.y - 8, 16, 16);
        g.setColour(colour);
        g.fillEllipse(screen.x - 4, screen.y - 4, 8, 8);

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

    const auto targetGlow = hovered == CloudIndex::none ? 0.0f : 1.0f;
    if (std::abs(glow - targetGlow) > 0.01f) glow += (targetGlow - glow) * 0.25f;
    else glow = targetGlow;

    const auto targetBlend = matches.empty() ? 0.0f : 1.0f;
    if (std::abs(filterBlend - targetBlend) > 0.002f) filterBlend += (targetBlend - filterBlend) * 0.2f;
    else filterBlend = targetBlend;

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
    const bool fast = animating || hasLensTarget || lensStrength > 0.01f
        || std::abs(glow - (hovered == CloudIndex::none ? 0.0f : 1.0f)) > 0.01f
        || std::abs(filterBlend - (matches.empty() ? 0.0f : 1.0f)) > 0.002f;
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
    if (! isPointVisible(found)) return; // dimmed by a filter: inspect, do not change the selection
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
