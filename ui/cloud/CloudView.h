#pragma once
#include "CloudIndex.h"
#include "core/cloud/CloudModel.h"
#include "ui/shell/StudioControls.h"
#include <functional>
#include <map>

namespace sonicator::cloud
{
// One component for the whole map: 35k points, no child component per point. The camera is a
// scale and a pan, navigation is animated, and the frame is drawn in screen coordinates so a
// point keeps the same size at every zoom level.
//
// The map is one cloud, not a ring of discs: islands are packed touching, there is no circle drawn
// around a tag, and a soft colour field under the points carries the gradients between them. The
// hover lens is a local magnifier: points near the cursor spread out, the rim fades through a
// feathered mask, and nothing about the hit test or the selection moves.
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
    // Ambient motion (the slow sway and the breathing field) can be switched off; the camera and
    // the hover lens keep working.
    void setMotionEnabled(bool enabled) { motion = enabled; }
    bool motionEnabled() const { return motion; }

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
    void onFrameForTest() { advance(16.0); }
    void hoverForTest(juce::Point<int> screen);
    void pointerForTest(juce::Point<int> screen) { lensTarget = screen.toFloat(); hasLensTarget = true; }
    void clearPointerForTest() { hasLensTarget = false; }
    void activatePointForTest(size_t index) { activate(index); }
    float lensStrengthForTest() const { return lensStrength; }
    float introProgressForTest() const { return introAlpha(); }
    // The displacement the lens gives one point, in screen pixels. The point's own coordinates and
    // the hit test never move.
    juce::Point<float> lensOffsetForTest(size_t index) const;

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
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
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
    void advance(double dtMs);
    void animateTo(Camera target);
    void pushHistory();
    void applyCamera(const Camera&);
    Camera cameraForFit() const;
    void rebuildColours();
    void rebuildLayers();
    void rebuildPointLayer();
    void rebuildLabels();
    void rebuildIslandCounts();
    float introAlpha() const;
    juce::Point<float> ambientSway() const;
    float pointDrift(size_t index, bool vertical) const;
    void drawLayer(juce::Graphics&, const juce::Image&, juce::Point<float> sway) const;
    void drawPointsLive(juce::Graphics&, const std::vector<size_t>& visible, bool matching,
                        juce::Point<float> sway, bool useLens, bool withDrift) const;
    void drawLabels(juce::Graphics&, const std::vector<size_t>& visible, juce::Point<float> sway);
    void ensureFrameCache(const std::vector<size_t>& visible);
    void paintDynamic(juce::Graphics&, juce::Point<float> sway);
    float lensMask(float distance) const;
    juce::Point<float> lensOffsetForScreen(juce::Point<float> screen) const;
    void setHovered(size_t index);
    void updateTimerRate();
    juce::Rectangle<float> worldViewport() const;
    void activate(size_t index);

    CloudLayout current;
    CloudIndex index;
    std::map<uint32_t, std::vector<size_t>> byColour; // built once per layout, keeps setColour rare
    std::vector<bool> matches;
    std::vector<int> islandVisibleCounts; // per island, how many points the filter leaves
    std::map<int64_t, juce::String> labels;
    std::vector<size_t> visibleScratch;
    // The visible set is recomputed only when the camera (not a hover) changes.
    std::vector<size_t> visibleCache;
    juce::Rectangle<float> visibleArea;
    bool visibleValid = false;
    // Static world-space layers: the map is redrawn only when it changes, so panning and zooming
    // blit an image instead of re-rasterising 35k points.
    juce::Image pointLayer, frameCache;
    Camera frameCamera;
    bool frameValid = false;
    float layerScale = 1.0f;
    // Label text and measured width are fixed for a layout; measuring them every frame costs more
    // than drawing the map.
    std::vector<juce::String> islandLabels;
    std::vector<int> islandLabelWidths;
    std::vector<juce::Image> islandLabelImages;
    std::vector<size_t> labelOrder;
    int selectedIndex = -1;

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
    bool hoverAudio = true;
    bool dragging = false;
    juce::Point<float> dragStart;
    juce::Point<double> dragOrigin;
    juce::Point<int> lastMousePosition;

    // Ambient motion and the hover lens.
    bool motion = true;
    double motionMs = 0.0;
    double introElapsedMs = 0.0;
    float labelAlpha = 0.0f;
    juce::uint32 lastMotionMs = 0;
    float lensStrength = 0.0f;
    float hoverGlow = 0.0f;
    juce::Point<float> lensPos, lensTarget;
    bool hasLensTarget = false;
    int currentTimerHz = 0;
    juce::uint32 lastTimerMs = 0;

    bool loading = false;
    juce::String loadingText;
    juce::String emptyText { "No tagged files yet" };
    juce::String notice;

    studio::Chip backButton { "Back" }, fitButton { "Overview" };
};
} // namespace sonicator::cloud
