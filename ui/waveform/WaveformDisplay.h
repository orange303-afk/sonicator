#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <vector>
#include "WaveformSelection.h"
#include "WaveformView.h"
#include "core/sampler/SamplerTypes.h"
#include "core/spectral/SpectralMask.h"
namespace sonicator {
class SpectrogramService;
struct SpectrogramGrid;
// The preview card draws either the waveform or a spectrogram. The card, the visible time range,
// the selection, the playhead, the overview strip and every mouse gesture are shared; only the
// content branch of paint() changes. The spectrum is analysed on a background worker, never in
// paint and never on the audio thread.
class WaveformDisplay final : public juce::Component, private juce::ChangeListener,
                              private juce::Timer
{
public:
    enum class RenderMode { peaks, spectrum };
    // How a drag inside the spectrum is interpreted. The time tool is the ordinary selection the
    // waveform uses; the other three build a time-frequency mask.
    enum class SelectionTool { time, rectangle, lasso, wand };
    void setRenderMode(RenderMode);
    RenderMode renderMode() const { return mode; }
    void setSelectionTool(SelectionTool);
    SelectionTool selectionTool() const { return tool; }
    void setSpectralCacheEnabled(bool enabled) { spectralCache = enabled; }
    // The committed 2D selection, or nullptr when there is none.
    const SpectralMask* spectralMask() const;
    void clearSpectralMask();
    std::function<void()> onSpectralMaskChanged;
    std::function<void()> onSelectionToolChanged;
    // Full decoded snapshot for sources the thumbnail cannot read (REX2, sampler, tracker, disc,
    // edits). `revision` distinguishes two snapshots of the same length.
    void setSpectrogramAudio(SampleAudioPtr, juce::uint64 revision = 0);
    void setSpectrumLogScale(bool);
    bool spectrumLogScale() const { return spectroLog; }
    // True when the current source can produce a spectrum at all.
    bool spectrumAvailable() const;
    // Test hooks: a committed 2D selection without synthesising mouse events.
    void setSpectralMaskForTest(const SpectralMask& value);
    // Test hook: the glyph the current spectrum tool shows (0 rect, 1 lasso, 2 wand, -1 none).
    int spectralCursorKindForTest() const
    {
        return tool == SelectionTool::time ? -1 : (int) tool - 1;
    }
    // Test hooks: what was requested and what arrived.
    juce::String spectrumRequestKeyForTest() const { return spectroRequestKey; }
    juce::String spectrumReadyKeyForTest() const { return spectroReadyKey; }
    bool spectrumImageReadyForTest() const
    {
        return overviewImage.isValid() || detailImage.isValid();
    }
    explicit WaveformDisplay(const juce::File& cacheFolder);
    ~WaveformDisplay() override;
    void setFile(const juce::File&);
    // REX2 and other decoded containers have no file the thumbnail can read, so their PCM is
    // reduced to min/max peaks here. The caller keeps ownership of the samples.
    void setPeaks(const std::vector<float>& samples, int sampleRate,
                  const std::vector<int64_t>& sliceStarts);
    // Shows edited PCM instead of the file thumbnail; `sliceStarts` is in frames.
    void setEditedAudio(SampleAudioPtr, const std::vector<int64_t>& sliceStarts = {});
    void clearEditedAudio();
    void setActiveSlices(std::vector<int> indices);
    void setPosition(double p) { position = p; repaint(); }
    void setSelection(selection::Range);
    selection::Range selection() const { return currentSelection; }
    // While on, a dragged selection edge lands on the nearest zero crossing of the source, so the
    // cut starts and ends at silence instead of at a step.
    void setSnapToZeroCrossing(bool enabled) { snapToZero = enabled; }
    bool snapToZeroCrossing() const { return snapToZero; }
    void clearSelection();
    // While armed (Loop on), dragging inside the range moves it and dragging an edge resizes it,
    // instead of starting the external DAW drag. The wheel always resizes the range around its
    // centre. Live edits call onSelectionChanging; the final range calls onSelectionChanged.
    void setLoopArmed(bool armed) { loopArmed = armed; }
    waveview::View view() const { return effectiveView(); }
    void setView(waveview::View);
    void zoomToFit();
    void zoomToSelection();
    // Zoom around one time position; factor > 1 zooms in. The gesture wrappers below are thin, so
    // the geometry stays testable without synthesising mouse events.
    void zoomAtTime(double seconds, double factor);
    void panBySeconds(double seconds);
    void setSampleRateForZoom(int rate) { zoomSampleRate = rate; }
    // The mask image carries the accent colour of the moment it was built.
    void lookAndFeelChanged() override { maskImage = {}; repaint(); }
    int sampleRate() const { return zoomSampleRate; }
    void mouseMagnify(const juce::MouseEvent&, float scaleFactor) override;
    double totalLength() const
    {
        if (editedAudio != nullptr && editedAudio->sampleRate > 0)
            return (double) editedAudio->frames / (double) editedAudio->sampleRate;
        return customLength > 0 ? customLength : thumbnail.getTotalLength();
    }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void(double)> onSeek;
    std::function<void(selection::Range)> onSelectionChanged;
    std::function<void(selection::Range)> onSelectionChanging;
    std::function<void()> onExternalDragRequested;
    // Screen position of a right-click; the owner builds and shows the menu.
    std::function<void(juce::Point<int>)> onContextMenu;
private:
    juce::File cacheFile;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 64 };
    juce::AudioThumbnail thumbnail { 512, formats, cache };
    std::unique_ptr<juce::AudioFormatReader> zoomReader;
    std::vector<float> peakMin, peakMax;
    SampleAudioPtr editedAudio;
    std::vector<int64_t> editedSlices;
    struct PeakColumn { float minLeft = 0, maxLeft = 0, minRight = 0, maxRight = 0; };
    std::vector<PeakColumn> columnCache;
    waveview::View cachedView;
    int cachedWidth = 0;
    std::vector<double> sliceTimes;
    std::vector<int> activeSlices;
    double customLength = 0;
    waveview::View viewRange;
    int zoomSampleRate = 0;
    double position = 0;
    selection::Range currentSelection;
    juce::Point<int> dragOrigin;
    double dragAnchor = 0;
    bool pressInsideSelection = false, selectionDragActive = false, externalDragTriggered = false;
    bool pressWasPrimary = false, loopArmed = false, contextMenuClick = false;
    bool snapToZero = false;
    enum class DragMode { none, select, move, resizeStart, resizeEnd, overview };
    DragMode dragMode = DragMode::none;
    selection::Range dragStartRange;
    double dragStartTime = 0;
    void updateSelectionDuringDrag(double time);
    // The zero-crossing snap of one edge; returns `seconds` when snapping is off or the source is
    // unavailable.
    double snappedTime(double seconds);
    void publishLiveSelection(selection::Range);
    void notifySelection();
    void notifySelectionChanging();
    double secondsPerPixel() const;
    waveview::View effectiveView() const;
    double minimumViewLength() const;
    double overviewDragStart = 0;
    bool isOverviewPoint(juce::Point<int>) const;
    double minimumSelectionLength() const;
    void updateCursorFor(double time);
    void drawEditedPeaks(juce::Graphics&, juce::Rectangle<float> area, waveview::View);
    void drawFileSamples(juce::Graphics&, juce::Rectangle<float> area, waveview::View);
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    // Spectrum. `sourceFile` survives an edit so clearing the edit falls back to the file.
    juce::File sourceFile;
    RenderMode mode = RenderMode::peaks;
    std::unique_ptr<SpectrogramService> spectroService;
    juce::File spectroFile;
    SampleAudioPtr spectroAudio;
    juce::String spectroSource, spectroRequestKey, spectroReadyKey, spectroError;
    juce::uint64 spectroRevision = 0;
    bool spectroLog = true;
    juce::uint32 spectroDirtyAt = 0;
    static constexpr int spectrumDebounceMs = 150;
    // Cached whole-file overview and the on-demand detail pass for a zoomed view.
    std::shared_ptr<const SpectrogramGrid> overviewGrid, detailGrid;
    juce::Image overviewImage, detailImage;
    bool pendingIsOverview = false, spectralCache = true, spectralGesture = false;
    SelectionTool tool = SelectionTool::time;
    std::unique_ptr<SpectralMask> mask;
    juce::Image maskImage;
    // The frequency axis the cached overlay image was built for; a zoom, a scale change or another
    // grid rebuilds it.
    double maskImageMinHz = 0, maskImageMaxHz = 0;
    bool maskImageLog = true;
    double gestureStartTime = 0, gestureEndTime = 0, gestureStartHz = 0, gestureEndHz = 0;
    std::vector<juce::Point<double>> lassoPointsHz;
    int wandToleranceDb = 6;
    // A press on the selection itself lifts it out instead of drawing over it.
    bool spectralPressInMask = false, spectralDragTriggered = false;

    void timerCallback() override;
    void drawSpectrum(juce::Graphics&, juce::Rectangle<float> area, waveview::View);
    void drawMask(juce::Graphics&, juce::Rectangle<float> area, waveview::View);
    void drawSpectralGesture(juce::Graphics&, juce::Rectangle<float> area, waveview::View);
    void drawSpectralToolbar(juce::Graphics&, juce::Rectangle<float> area);
    void invalidateSpectrum(bool dropResult);
    void ensureSpectrum(waveview::View, juce::Rectangle<float> area);
    juce::String spectrumKey(waveview::View, juce::Rectangle<float> area, bool overview) const;
    juce::Rectangle<float> contentArea() const;
    double sourceDuration() const;
    SpectrogramService& spectrumService();
    const SpectrogramGrid* displayedGrid(waveview::View) const;
    const juce::Image* displayedImage(waveview::View) const;
    void refreshFromCache();
    void rebuildMaskImage();
    bool pointInMask(juce::Point<float> position, juce::Rectangle<float> area,
                     waveview::View view) const;
    void refreshSpectrumCursor(bool overMask);
    void mouseExit(const juce::MouseEvent&) override;
    void beginSpectralGesture(const juce::MouseEvent&, juce::Rectangle<float> area,
                              waveview::View view);
    void updateSpectralGesture(const juce::MouseEvent&, juce::Rectangle<float> area,
                               waveview::View view);
    void commitSpectralGesture(const juce::MouseEvent&, juce::Rectangle<float> area,
                               waveview::View view);
    void commitMaskWindow(double start, double end, bool replace);
    void runWand(const juce::MouseEvent&, juce::Rectangle<float> area, waveview::View view);
    double frequencyAtY(float y, juce::Rectangle<float> area, waveview::View) const;
    double nyquist() const;
    int spectralToolbarHit(int x, int y) const;
    juce::Rectangle<int> spectralToolbarBounds() const;
    void notifyMaskChanged();
};
} // namespace sonicator
