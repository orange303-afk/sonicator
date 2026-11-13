#pragma once
#include "core/audio/AudioEngine.h"
#include "core/settings/AppSettings.h"
#include "core/export/SelectionExporter.h"
#include "ui/waveform/WaveformDisplay.h"
#include "core/edit/AudioEditBuffer.h"
#include "SongProgress.h"
namespace sonicator {
// Selector that also steps with Up/Down and the mouse wheel while closed. The popup keeps its
// own keyboard behaviour when open.
class StepComboBox final : public juce::ComboBox
{
public:
    void step(int delta)
    {
        const int count = getNumItems();
        if (count <= 0) return;
        const int index = juce::jlimit(0, count - 1, getSelectedItemIndex() + delta);
        if (index != getSelectedItemIndex())
            setSelectedItemIndex(index, juce::sendNotificationSync);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY > 0.001f) step(-1);
        else if (wheel.deltaY < -0.001f) step(1);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::upKey) { step(-1); return true; }
        if (key.getKeyCode() == juce::KeyPress::downKey) { step(1); return true; }
        return juce::ComboBox::keyPressed(key);
    }
};
// A button that starts an external drag of an already prepared WAV. The drag must begin inside a
// real mouse gesture, so preparation is a separate step performed when the button is pressed.
class SampleDragButton final : public juce::TextButton
{
public:
    SampleDragButton() : juce::TextButton("Drag sample") {}
    std::function<void()> onDrag;
    void mouseUp(const juce::MouseEvent&) override { dragStarted = false; }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! dragStarted && event.getDistanceFromDragStart() > 5 && onDrag)
        {
            dragStarted = true;
            onDrag();
        }
    }
private:
    bool dragStarted = false;
};
class PreviewPanel final : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    PreviewPanel(AudioEngine&, const juce::File& cacheFolder,
                 const juce::File& exportFolder = AudioExporter::defaultExportFolder());
    ~PreviewPanel() override { stopTimer(); }
    // allowAutoPlay=false loads the file and waveform without starting playback. Used when the
    // last session is restored, so reopening the app does not unexpectedly audition audio.
    bool loadFile(const juce::File&, bool allowAutoPlay = true);
    // Loads one decoded source from a disc image. The container is the physical image (used only
    // as export identity); the virtual entry never becomes an OS path.
    bool loadDiscSample(const juce::File& container, const juce::String& stem, SampleAudioPtr,
                        bool looping, int64_t loopStart, int64_t loopEnd, bool pingPong,
                        bool allowAutoPlay = true);
    void clearDiscSource();
    // Stops playback and returns the panel to the empty state. Used when the library selection
    // is cleared or holds more than one sample, so no stale waveform or audio remains.
    void clearSource();
    SampleAudioPtr discSampleAudio() const { return discAudioRef; }
    // Restores preview toggles and their engine state. Does not notify onSettingsChanged.
    void applyPersistedSettings(const AppSettings&);
    bool midiPreviewEnabled() const { return engine.isMidiPreview(); }
    bool skipSilenceEnabled() const { return engine.isSkipSilence(); }
    bool autoPreviewEnabled() const { return autoPreview.getToggleState(); }
    bool loopEnabled() const { return loop.getToggleState(); }
    bool rexGateEnabled() const { return rexGate.getToggleState(); }
    int soundFontSampleCount() const { return (int) instrumentSamples.size(); }
    SampleId selectedSoundFontSampleId() const { return currentSampleId; }
    int instrumentSampleCount() const { return (int) instrumentSamples.size(); }
    SampleId selectedInstrumentSampleId() const { return currentSampleId; }
    // Tracker whole-song versus selected-source workflow. Selecting a source stops the song and
    // keeps its position; the whole-song item resumes it from that position.
    int trackerSampleCount() const { return (int) engine.trackerSlots().size(); }
    SampleId selectedTrackerSampleId() const { return engine.selectedTrackerSampleId(); }
    bool trackerWholeSongSelected() const
    { return engine.isTrackerMode() && ! engine.isTrackerSampleSelected(); }
    bool selectTrackerSlot(SampleId);
    void selectTrackerWholeSong();
    void selectRange(double start, double end);
    selection::Range selectedRange() const { return waveform.selection(); }
    void clearSelection() { waveform.clearSelection(); }
    bool isSelectionReady() const;
    // Waveform editing. The buffer lives in memory; the file changes only on an explicit save.
    bool hasUnsavedEdits() const { return editBuffer.has_value() && editBuffer->isModified(); }
    bool sourceIsEditable() const;
    enum class EditCommand { undo, redo, deleteRegion, crop, silence, reverse, fadeIn, fadeOut,
                             normalize, gain, toMono, removeDc, trimSilence, zoomToSelection,
                             zoomToFit, save };
    void applyEditCommand(EditCommand);
    void saveEdits(std::function<void(bool)> done = {});
    // Calls `done(true)` now when nothing is unsaved; otherwise asks Save/Discard/Cancel and calls
    // `done(true)` only after a successful save or an explicit discard, `done(false)` on Cancel.
    void withUnsavedEditsConfirmed(std::function<void(bool)> done);
    void saveEditsAsCopyForTest();
    void overwriteEditsForTest();
    void discardEditsForTest();
    int64_t editingSampleId = 0;
    std::function<void(const juce::File& target, int64_t sampleId)> onFileSavedInPlace;
    std::function<void(const juce::File& copy, int64_t templateSampleId)> onCopySaved;
    // True when the transport owns a decoded disc-image sample (used to ignore stale previews).
    bool isDiscPreview() const { return engine.isDiscMode(); }
    // Applies the last source that really started on the instrument when no selection pins the
    // waveform. The preview timer calls this; tests call it directly.
    void refreshMidiFollow();
    juce::File preparedSelection() const { return exporter.state().file; }
    SelectionExporter::State selectionExportState() const { return exporter.state(); }
    bool waitForSelectionPreparation(int timeoutMs);
    void updateSelectionPlayback();
    double waveformLength() const { return waveform.totalLength(); }
    std::function<void()> onAction;
    juce::ListBox& containerSourceList() { return sourceList; }
    std::function<void()> onSourcesChanged;
    // Called after a user changes one of the persisted preview toggles.
    std::function<void()> onSettingsChanged;
    void resized() override;
    void lookAndFeelChanged() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    AudioEngine& engine;
    juce::File loadedFile;
    int64_t loadedSize = 0, loadedModified = 0;
    WaveformDisplay waveform;
    SongProgress songProgress;
    juce::Label filename, time;
    juce::Label reasonInfo, abletonInfo;
    bool applyReasonReference(int, bool autoPlay);
    juce::TextButton play { "Play" }, stop { "Stop" };
    StepComboBox presetBox;
    StepComboBox sampleBox;
    StepComboBox trackerBox;
    juce::ListBox sourceList { "Container sources", this };
    bool instrumentHead = true, syncingSources = false;
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void refreshSourceList();
    SampleDragButton sampleDragButton;
    std::vector<SampleDescriptor> instrumentSamples;
    SampleId currentSampleId = invalidSampleId;
    SampleAudioPtr discAudioRef;
    juce::String discStem;
    juce::ToggleButton rexGate { "Gate" };
    juce::Label rexInfo;
    juce::ToggleButton loop { "Loop" };
    juce::ToggleButton midi { "MIDI Preview" }, skipSilence { "Skip Silence" };
    juce::ToggleButton autoPreview { "Auto Preview" };
    juce::Slider volume;
    SelectionExporter exporter;
    std::optional<edit::AudioEditBuffer> editBuffer;
    juce::File editTarget;
    juce::String editStem;
    bool ensureEditBuffer();
    SampleAudioPtr currentEditableAudio() const;
    void afterEditApplied();
    void refreshDirtyMark();
    void showWaveformMenu(juce::Point<int> screenPosition);
    void overwriteEdits(std::function<void(bool)> done);
    void saveCopyAndSwitch(std::function<void(bool)> done);
    void discardEdits();
    juce::String editingName() const;
    juce::TextButton clearSelectionButton { "Clear" };
    juce::Label selectionStatus;
    void timerCallback() override;
    void togglePlayback();
    void applyTransportLooping();
    void selectionChanging(selection::Range);
    void selectionChanged(selection::Range);
    void prepareSelectionExport();
    void followPlayheadInto(selection::Range);
    void refreshSelectionStatus();
    bool selectionExportMatches(const SelectionExporter::State&) const;
    juce::uint32 selectionEditedMs = 0;
    bool exportPending = false;
    selection::Range liveRange;
    void beginExternalDrag();
    void refreshInstrumentSamples();
    bool applyInstrumentSample(SampleId, bool manual);
    juce::String instrumentSampleStem() const;
    void fillTrackerChoices();
    bool applyTrackerSample(SampleId, bool manual);
    void applyTrackerWholeSong(bool manual);
    juce::String trackerSummary() const;
    juce::String trackerSampleSummary() const;
    juce::String trackerSampleStem() const;
    void prepareWholeTrackerSample();
    juce::String discSampleSummary() const;
    void prepareWholeDiscSample();
};
} // namespace sonicator
