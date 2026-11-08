#pragma once
#include "core/audio/AudioEngine.h"
#include "core/settings/AppSettings.h"
#include "core/export/SelectionExporter.h"
#include "ui/waveform/WaveformDisplay.h"
namespace sonicator {
class PreviewPanel final : public juce::Component, private juce::Timer
{
public:
    PreviewPanel(AudioEngine&, const juce::File& cacheFolder,
                 const juce::File& exportFolder = AudioExporter::defaultExportFolder());
    ~PreviewPanel() override { stopTimer(); }
    // allowAutoPlay=false loads the file and waveform without starting playback. Used when the
    // last session is restored, so reopening the app does not unexpectedly audition audio.
    bool loadFile(const juce::File&, bool allowAutoPlay = true);
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
    void selectRange(double start, double end);
    selection::Range selectedRange() const { return waveform.selection(); }
    void clearSelection() { waveform.clearSelection(); }
    bool isSelectionReady() const;
    // Applies the last source that really started on the instrument when no selection pins the
    // waveform. The preview timer calls this; tests call it directly.
    void refreshMidiFollow();
    juce::File preparedSelection() const { return exporter.state().file; }
    SelectionExporter::State selectionExportState() const { return exporter.state(); }
    bool waitForSelectionPreparation(int timeoutMs);
    void updateSelectionPlayback();
    double waveformLength() const { return waveform.totalLength(); }
    std::function<void()> onAction;
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
    juce::Label filename, time;
    juce::TextButton play { "Play" }, stop { "Stop" };
    juce::ComboBox presetBox;
    juce::ComboBox sampleBox;
    std::vector<SampleDescriptor> instrumentSamples;
    SampleId currentSampleId = invalidSampleId;
    juce::ToggleButton rexGate { "Gate" };
    juce::Label rexInfo;
    juce::ToggleButton loop { "Loop" };
    juce::ToggleButton midi { "MIDI Preview" }, skipSilence { "Skip Silence" };
    juce::ToggleButton autoPreview { "Auto Preview" };
    juce::Slider volume;
    SelectionExporter exporter;
    juce::TextButton clearSelectionButton { "Clear" };
    juce::Label selectionStatus;
    void timerCallback() override;
    void togglePlayback();
    void applyTransportLooping();
    void selectionChanged(selection::Range);
    void refreshSelectionStatus();
    void beginExternalDrag();
    void refreshInstrumentSamples();
    bool applyInstrumentSample(SampleId, bool manual);
    juce::String instrumentSampleStem() const;
};
} // namespace sonicator
