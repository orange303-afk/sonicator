#pragma once
#include "core/audio/AudioEngine.h"
#include "ui/shell/StudioControls.h"
#include "core/settings/AppSettings.h"
#include "core/export/SelectionExporter.h"
#include "ui/waveform/WaveformDisplay.h"
#include "core/edit/AudioEditBuffer.h"
#include "EditCommands.h"
#include "SongProgress.h"
#include "PluginMatchControls.h"
#include "core/match/MatchRenderService.h"
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
    // The state the Play/Pause caption follows. Tests assert the engine directly: the caption is
    // refreshed by the 30 Hz panel timer, so it can lag one tick behind the transport.
    bool transportPlaying() const { return engine.isPlaying(); }
    // Restores preview toggles and their engine state. Does not notify onSettingsChanged.
    void applyPersistedSettings(const AppSettings&);
    // Settings -> Editing. Kept separate so a theme change does not reapply every preview toggle.
    void setAlwaysOverwriteOriginalFiles(bool overwrite) { alwaysOverwrite = overwrite; }
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
    // The red marker for a source row that cannot be auditioned: an FM (AdLib) slot or one whose
    // decode failed. Exposed so the colour contract can be checked without depending on the host's
    // monospaced typeface, which a headless Windows host may not have.
    static bool trackerSlotUnavailable(const TrackerSampleSlot& slot)
    { return slot.fmInstrument || slot.error.isNotEmpty(); }
    static juce::Colour unavailableSourceColour() { return juce::Colour(0xffe05a5a); }
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
    // True when this panel holds unsaved edits of that file. The library's Normalize selected
    // checks it so a batch never rewrites the file the editor still owns.
    // The file the panel currently shows; used to refresh it after a library Normalize.
    juce::File loadedFilePath() const { return loadedFile; }
    bool hasUnsavedEditsFor(const juce::File&) const;
    bool sourceIsEditable() const;
    using EditCommand = EditAction;
    void applyEditCommand(EditCommand);
    // Transport entry points for the command registry. The buttons call the same code.
    void playPause() { togglePlayback(); }
    void stopPlayback();
    // Escape: clears the selection and stops the transport, except in instrument mode where the
    // pin is lifted and the MIDI voices keep sounding.
    void stopAndClearSelection();
    void seekBy(double seconds) { engine.seek(engine.position() + seconds); }
    // Set by MainComponent: the window router runs a bound key before the panel's own handling.
    std::function<bool(const juce::KeyPress&)> onCommandKey;
    void saveEdits(std::function<void(bool)> done = {});
    // Calls `done(true)` now when nothing is unsaved; otherwise asks Save/Discard/Cancel and calls
    // `done(true)` only after a successful save or an explicit discard, `done(false)` on Cancel.
    void withUnsavedEditsConfirmed(std::function<void(bool)> done);
    // Tap-tempo playback: loops the waveform selection (or the whole source) while the user taps.
    void startTapLoop();
    void stopTapLoop();
    bool canLoopForTap() const;
    void saveEditsAsCopyForTest();
    void overwriteEditsForTest();
    void discardEditsForTest();
    // Plugin-only Match BPM / Match Scale. Installing a context reveals the controls; the
    // standalone never calls this, so no toggle exists there.
    void setPluginMatchContext(PluginMatchContext);
    // Applies a project state the host loaded into an open editor without writing it back.
    void applyPluginMatchPreferences(const PluginMatchPreferences&);
    bool matchControlsVisible() const { return matchControls != nullptr; }
    bool matchedPreviewActive() const { return matchApplied; }
    juce::String matchDragBlockMessage() const { return matchBlockMessage; }
    // Waits for the running preparation and applies it, as the panel timer would. Tests call it
    // directly instead of sleeping.
    bool waitForMatchedPreview(int timeoutMs);
    // Runs the same Match part of the panel timer with a caller-supplied clock, so a test can
    // exercise tempo debounce and invalidation without sleeping on the UI timer.
    void tickMatchForTest(juce::uint64 nowMs) { tickMatch(nowMs); }
    bool matchedPreviewIsCurrent() const;
    juce::File matchedPreviewFile() const { return matchPreparedFile; }
    PluginMatchControls* matchPanel() { return matchControls.get(); }
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
    juce::Label filename, time, formatLine;
    juce::Label reasonInfo, abletonInfo;
    bool applyReasonReference(int, bool autoPlay);
    juce::TextButton play { "Play" };
    studio::IconButton stop { ActionIcon::stop, "Stop" };
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
    bool discLooping = false;
    int64_t discLoopStart = 0, discLoopEnd = 0;
    bool clearSelectionAfterEdit = false, checkClipping = false;
    // Settings -> Editing: skip the Save dialog and replace a writable original directly.
    bool alwaysOverwrite = false;
    studio::CheckChip rexGate { "Gate", true };
    juce::Label rexInfo;
    studio::CheckChip loop { "Loop", true };
    studio::CheckChip midi { "MIDI Preview", true }, skipSilence { "Skip Silence", true };
    studio::CheckChip autoPreview { "Auto Preview", true };
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
    void resetEditSession();
    juce::String editingName() const;
    // Filled where the file is already open, never from the timer.
    void refreshFormatLine();
    juce::String sourceFormat;
    int sourceRate = 0, sourceChannels = 0, sourceBits = 0;
    bool sourceIsMono() const;
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
    bool loopDragBlocked = false;
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
    // Match state. The plan is the single decision shared by preview, waveform and drag.
    std::optional<PluginMatchContext> matchContext;
    std::unique_ptr<PluginMatchControls> matchControls;
    std::unique_ptr<MatchRenderService> matchService;
    std::unique_ptr<MatchRequestScheduler> matchScheduler;
    PluginMatchPreferences appliedMatchPreferences;
    MatchSource matchSource;
    MatchPlan matchPlan;
    juce::String matchDesiredKey, matchActivatedKey, matchBlockMessage;
    juce::File matchPreparedFile;
    juce::File matchFolder;
    int64_t matchSourceSize = 0, matchSourceModified = 0;
    bool matchApplied = false, matchAutoPlayPending = false, suppressMatchAutoPlay = false;
    juce::uint64 lastTempoRevision = 0, lastMatchIdentityCheckMs = 0;
    // The snapshot the last plan used, so a value change (including becoming invalid) re-plans
    // while a steady host tempo does not.
    double matchTempoBpm = 0.0;
    bool matchTempoValid = false;
    void commitMatchPreferences(const PluginMatchPreferences&);
    void refreshMatch(bool debounceTempo, bool allowAutoPlay = true);
    void tickMatch(juce::uint64 nowMs);
    void pollMatch();
    void clearMatchedPreview(bool restoreMidiSelection);
    void activateMatchedPreview(const MatchRenderService::State&);
    juce::String matchStatusText() const;
};
} // namespace sonicator
