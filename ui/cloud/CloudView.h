#pragma once
#include "CloudIndex.h"
#include "core/cloud/CloudModel.h"
#include "ui/shell/StudioControls.h"
#include "core/cloud/CloudModel.h"
#include <functional>
#include <map>

namespace sonicator::cloud
{
// One component for the whole map: 35k points, no child component per point. The camera is a
// scale and a pan, navigation is animated, and the frame is drawn in screen coordinates so a
// point keeps the same size at every zoom level.
class CloudView final : public juce::Component, private juce::Timer
{
public:
    CloudView();
    ~CloudView() override;

    void setLayout(CloudLayout layout);
    const CloudLayout& layout() const { return current; }
    bool hasLayout() const { return current.valid(); }
    // Parallel to layout().points; an empty vector means "everything matches".
    void setFilter(std::vector<bool> matches);
    void setSelectedSample(int64_t sampleId);
    // Caption text per sample (name, tags, duration, format), prepared by the owner on hover.
    void setLabel(int64_t sampleId, juce::String text);
    void setLoading(bool loading, const juce::String& message = {});
    void setEmptyMessage(const juce::String&);
    // A short line over the map (for example "Nothing matches the filter"); empty clears it. The
    // map itself is never replaced, so the context stays visible.
    void setNotice(const juce::String& text);
    void setHoverAudioEnabled(bool enabled) { hoverAudio = enabled; }

    void fitAll();
    void zoomBy(float factor, juce::Point<float> screenPoint);
    void cameraBack();
    bool canGoBack() const { return ! history.empty(); }
    void focusOnSample(int64_t sampleId);

    float scale() const { return (float) viewScale; }
    juce::Point<float> worldToScreen(float x, float y) const;
    juce::Point<float> screenToWorld(juce::Point<float>) const;
    size_t pointAtScreen(juce::Point<int>) const;
    bool isPointVisible(size_t index) const;

    // Test seams: the mouse path without a synthesised MouseEvent.
    void onFrameForTest() { timerCallback(); }
    void hoverForTest(juce::Point<int> screen) { setHovered(pointAtScreen(screen)); }
    void activatePointForTest(size_t index) { activate(index); }

    std::function<void(int64_t)> onActivate;
    std::function<void(int64_t)> onHover;
    std::function<void()> onHoverEnd;
    // Dragging a point out is the same non-destructive file drag as a library row: the OS gets a
    // copy request, never a move, and the EBL cache is reused.
    std::function<bool(int64_t, juce::Component*)> onDragOut;
    // The row menu, anchored at the point: same commands as the list.
    std::function<void(int64_t, juce::Point<int>)> onContextMenu;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Camera
    {
        double scale = 1.0;
        juce::Point<double> pan;
        bool operator==(const Camera& other) const
        {
            return std::abs(scale - other.scale) < 1e-6 && pan.getDistanceFrom(other.pan) < 0.01;
        }
    };
    void timerCallback() override;
    void animateTo(Camera target);
    void pushHistory();
    void applyCamera(const Camera&);
    Camera cameraForFit() const;
    void rebuildColours();
    void rebuildLayers();
    void drawLayer(juce::Graphics&, const juce::Image&) const;
    void drawPointsLive(juce::Graphics&, const std::vector<size_t>& visible, bool matching) const;
    void paintStatic(juce::Graphics&);
    void paintDynamic(juce::Graphics&);
    void setHovered(size_t index);
    juce::Rectangle<float> worldViewport() const;
    void drawPoint(juce::Graphics&, size_t index, juce::Point<float> position, float alpha);
    void drawHover(juce::Graphics&);
    void activate(size_t index);

    CloudLayout current;
    CloudIndex index;
    std::map<uint32_t, std::vector<size_t>> byColour; // built once per layout, keeps setColour rare
    std::vector<bool> matches;
    std::map<int64_t, juce::String> labels;
    std::vector<size_t> visibleScratch;
    // Static layers in world space: the map is redrawn only when it changes, so panning and
    // zooming blit an image instead of re-rasterising 35k points and 46 tinted discs per frame.
    juce::Image islandLayer, pointLayer, overviewLayer;
    // The composed frame for the current camera. Hover and selection repaint against it instead of
    // re-blitting the whole map, and the labels hide while the camera moves.
    juce::Image frameCache;
    Camera frameCamera;
    bool frameValid = false;
    juce::uint32 lastMotionMs = 0;
    void ensureFrameCache();
    bool frameIsCurrent() const;
    // Label text and measured width are fixed for a layout; measuring them every frame costs more
    // than drawing the map.
    std::vector<juce::String> islandLabels;
    std::vector<int> islandLabelWidths;
    // Labels are shaped once per layout and blitted afterwards: 46 shaped texts per frame cost
    // more than the whole map.
    std::vector<juce::Image> islandLabelImages;
    float layerScale = 1.0f;

    int lastWidth = 0, lastHeight = 0;
    double viewScale = 1.0;
    juce::Point<double> viewPan;
    Camera target;
    bool animating = false;
    std::vector<Camera> history;

    size_t hovered = CloudIndex::none;
    size_t pressedPoint = CloudIndex::none;
    bool draggedOut = false;
    int64_t selectedSample = 0;
    float glow = 0.0f;
    bool hoverAudio = true;
    bool dragging = false;
    juce::Point<float> dragStart;
    juce::Point<double> dragOrigin;

    bool loading = false;
    juce::String loadingText;
    juce::String emptyText { "No tagged files yet" };
    juce::String notice;

    studio::Chip backButton { "Back" }, fitButton { "Overview" };
};
}
