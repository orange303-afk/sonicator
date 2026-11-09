#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "MidiSampler.h"
#include "SampleAudition.h"
#include "core/sf2/Sf2Bank.h"
#include "core/sf2/Sf2KitSampler.h"
#include "core/rex/Rex2SlicePlayer.h"
#include "core/sampler/FileInstrument.h"
#include "core/sampler/ExternalKitSampler.h"
#include "core/audio/SampleVoicePlayer.h"
#include "core/tracker/TrackerDocument.h"
#include "core/tracker/TrackerSongRenderer.h"
#include "core/reason/ReasonPatch.h"
namespace sonicator {
// Control methods are message-thread only. transport() also permits offline rendering.
class AudioEngine
{
public:
    explicit AudioEngine(bool openDevice = true);
    ~AudioEngine();
    bool loadFile(const juce::File&);
    bool isReasonMode() const { return reasonMode; }
    const ReasonPatch& reasonPatch() const { return reasonMetadata; }
    int selectedReasonReference() const { return reasonReference; }
    bool selectReasonReference(int);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void setVolume(float);
    void setLooping(bool);
    bool setMidiPreview(bool);
    // SF2 banks are presets of zones, not a single audio file. previewSoundFont selects a drum
    // preset (first drum bank if index < 0) and routes MIDI Preview to the kit sampler.
    bool previewSoundFont(const juce::File&, int presetIndex = -1);
    bool isSoundFontMode() const { return sf2Mode; }
    juce::String soundFontPresetName() const { return sf2Sampler.presetName(); }
    int soundFontPresetCount() const { return (int) sf2Bank.presets().size(); }
    int soundFontPresetIndex() const { return sf2Sampler.presetIndex(); }
    juce::String soundFontPresetLabel(int index) const;
    bool selectSoundFontPreset(int index);
    // Unique audio sources used by the current preset, in deterministic first-use order. The
    // selected source feeds the waveform, ordinary Play and WAV export; MIDI keeps the preset map.
    const std::vector<SampleDescriptor>& soundFontSamples() const { return sf2Samples; }
    std::vector<SampleDescriptor> soundFontSamples(juce::String& error) const;
    SampleId soundFontSampleId() const { return sf2SampleId; }
    SampleAudioPtr soundFontSampleAudio() const { return sf2Sample; }
    bool selectSoundFontSample(SampleId);
    int soundFontActiveVoices() const { return sf2Sampler.activeVoices(); }
    juce::uint64 soundFontGeneration() const { return sf2Generation; }
    TriggerTelemetry::Snapshot lastSoundFontTrigger() const { return sf2Sampler.lastTrigger(); }
    // Built-in external-file instruments (EXS24 and supported NKI XML) share the sample browser,
    // audition and export behaviour with SF2. Presets stay SF2-only.
    bool isExternalInstrumentMode() const { return externalMode; }
    bool isSamplerInstrumentMode() const { return sf2Mode || externalMode; }
    const std::vector<SampleDescriptor>& instrumentSamples() const
    { return sf2Mode ? sf2Samples : externalInstrument.sources; }
    SampleId instrumentSampleId() const { return sf2Mode ? sf2SampleId : externalSampleId; }
    SampleAudioPtr instrumentSampleAudio() const { return sf2Mode ? sf2Sample : externalSample; }
    bool selectInstrumentSample(SampleId id)
    { return sf2Mode ? selectSoundFontSample(id) : selectExternalSample(id); }
    juce::uint64 instrumentGeneration() const { return sf2Mode ? sf2Generation : externalGeneration; }
    TriggerTelemetry::Snapshot lastInstrumentTrigger() const
    { return sf2Mode ? sf2Sampler.lastTrigger() : externalSampler.lastTrigger(); }
    int instrumentActiveVoices() const
    { return sf2Mode ? sf2Sampler.activeVoices() : externalSampler.activeVoices(); }
    int lastInstrumentMidiNote() const
    { return sf2Mode ? sf2Sampler.midiMonitorNote() : externalSampler.midiMonitorNote(); }
    int lastInstrumentMidiLayers() const
    { return sf2Mode ? sf2Sampler.midiMonitorLayers() : externalSampler.midiMonitorLayers(); }
    juce::String instrumentName() const
    { return sf2Mode ? sf2Sampler.presetName() : externalInstrument.name; }
    juce::StringArray instrumentWarnings() const { return externalInstrument.warnings; }
    // REX2 slices are a MIDI drum map starting at startNote (default 36). Gate stops a slice on
    // NoteOff; one-shot lets it ring out.
    bool previewRex2(const juce::File&, int startNote = 36);
    // Legacy tracker modules: the whole song and its individual source samples are separate
    // capabilities of one document. Selecting a sample stops the song but keeps its position.
    bool isTrackerMode() const { return trackerMode; }
    bool isTrackerSampleSelected() const { return trackerSampleChosen; }
    const TrackerDocumentInfo& trackerInfo() const;
    const std::vector<TrackerSampleSlot>& trackerSlots() const;
    SampleId selectedTrackerSampleId() const { return trackerSampleIdValue; }
    SampleAudioPtr trackerSampleAudio() const { return trackerSample; }
    juce::uint64 trackerSourceGeneration() const { return trackerGenerationValue; }
    int trackerUnderruns() const { return trackerSong.underruns(); }
    int trackerSampleActiveVoices() const { return sampleVoices.activeVoices(); }
    bool selectTrackerSample(SampleId);
    bool selectTrackerWholeSong();
    juce::String trackerSongTitle() const { return trackerSong.title(); }
    juce::String trackerSongFormat() const { return trackerSong.formatName(); }
    int trackerSongChannels() const { return trackerSong.channels(); }
    // A decoded source from a disc image. Ordinary Play auditions the whole immutable snapshot;
    // MIDI plays it chromatically with the same retained-source rules as a tracker sample.
    bool isDiscMode() const { return discMode; }
    SampleAudioPtr discSampleAudio() const { return discSample; }
    juce::uint64 discGeneration() const { return discGenerationValue; }
    int discActiveVoices() const { return sampleVoices.activeVoices(); }
    bool setDiscSample(const SampleAudioPtr&, bool looping, int64_t loopStart, int64_t loopEnd,
                       bool pingPong);
    void clearDiscSample();
    bool isRex2Mode() const { return rexMode; }
    int rexSliceCount() const { return rexSampler.sliceCount(); }
    int rexStartNote() const { return rexSampler.startNote(); }
    bool rexGateMode() const { return rexSampler.gateMode(); }
    std::shared_ptr<const Rex2Audio> rexAudio() const { return rexSampler.audio(); }
    std::vector<int64_t> rexSliceStarts() const { return rexSampler.sliceStarts(); }
    int rexLastMidiNote() const { return rexSampler.lastMidiNote(); }
    int rexActiveVoices() const { return rexSampler.activeVoices(); }
    float rexDecodedPeak() const { return rexSampler.decodedPeak(); }
    float rexLastVelocity() const { return rexSampler.lastVelocity(); }
    float rexMixPeak() const { return rexSampler.lastMixPeak(); }
    float outputPeak() const { return output.lastOutputPeak.load(); }
    float outputGain() const { return output.gain.load(); }
    float rexFirstEnvelope() const { return rexSampler.firstVoiceEnvelope(); }
    double rexFirstPosition() const { return rexSampler.firstVoicePosition(); }
    std::vector<int> rexActiveSlices() const { return rexSampler.activeSlices(); }
    void setRexGateMode(bool enabled) { rexSampler.setGateMode(enabled); }
    bool isMidiPreview() const { return output.midi.load(); }
    void setSkipSilence(bool enabled) { skipSilence = enabled; sampler.skipSilence = enabled; }
    bool isSkipSilence() const { return skipSilence; }
    bool isPlaying() const { if (trackerMode) return trackerSampleChosen ? sf2Audition.isPlaying() : trackerSong.isPlaying();
                             return auditionMode() ? sf2Audition.isPlaying() : rexMode ? rexSampler.auditionPlaying() : source.isPlaying(); }
    bool isLooping() const { return looping; }
    float getVolume() const { return source.getGain(); }
    double position() const { if (trackerMode) return trackerSampleChosen ? sf2Audition.position() : trackerSong.position();
                              return auditionMode() ? sf2Audition.position() : rexMode ? rexSampler.auditionSeconds() : source.getCurrentPosition(); }
    double length() const { if (trackerMode) return trackerSampleChosen ? sf2Audition.length() : trackerSong.length();
                            return auditionMode() ? sf2Audition.length() : rexMode ? rexSampler.lengthSeconds() : source.getLengthInSeconds(); }
    juce::String error() const { return lastError; }
    juce::AudioTransportSource& transport() { return source; }
    juce::AudioDeviceManager& deviceManager() { return devices; }
    // Offline render of the same PreviewOutput used by the device callback. Tests use it to
    // verify the complete MIDI -> sampler -> mix path without an audio device.
    void preparePreviewBlock(int blockSize);
    void renderPreviewBlock(juce::AudioBuffer<float>&, int numSamples);
    MidiEventQueue& midiCollector() { return output.collector; }
private:
    ReasonPatch reasonMetadata;
    bool reasonMode = false;
    int reasonReference = -1;
    bool deviceMode, looping = false;
    bool skipSilence = false, startAtNextPlay = true, midiInputsConnected = false;
    double audibleStart = -1;
    juce::File loadedFile;
    double inputRate = 0;
    juce::String lastError;
    juce::AudioFormatManager formats;
    juce::AudioDeviceManager devices;
    juce::TimeSliceThread readAhead { "Preview read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioTransportSource source;
    MidiSampler sampler;
    Sf2KitSampler sf2Sampler;
    Sf2Bank sf2Bank;
    std::vector<SampleDescriptor> sf2Samples;
    SampleId sf2SampleId = invalidSampleId;
    SampleAudioPtr sf2Sample;
    bool sf2Mode = false;
    juce::uint64 sf2Generation = 0;
    ExternalKitSampler externalSampler;
    FileInstrument externalInstrument;
    SampleId externalSampleId = invalidSampleId;
    SampleAudioPtr externalSample;
    bool externalMode = false;
    juce::uint64 externalGeneration = 0;
    Rex2SlicePlayer rexSampler;
    bool rexMode = false;
    SampleAudition sf2Audition;
    std::unique_ptr<TrackerDocument> trackerDocument;
    TrackerSongRenderer trackerSong;
    SampleVoicePlayer sampleVoices;
    bool trackerMode = false, trackerSampleChosen = false;
    SampleId trackerSampleIdValue = invalidSampleId;
    SampleAudioPtr trackerSample;
    juce::uint64 trackerGenerationValue = 0;
    bool discMode = false;
    SampleAudioPtr discSample;
    juce::uint64 discGenerationValue = 0;
    PreviewOutput output {source,sampler,sf2Sampler,rexSampler,sf2Audition,externalSampler,trackerSong,sampleVoices};
    juce::AudioSourcePlayer player;
    void attach();
    void clearContainers();
    bool refreshSoundFontSamples(bool selectFirstPlayable);
    bool previewFileInstrument(const juce::File&);
    bool previewTracker(const juce::File&);
    bool refreshExternalSamples(bool selectFirstPlayable);
    bool selectExternalSample(SampleId);
    bool auditionMode() const { return sf2Mode || externalMode || discMode; }
    double deviceSampleRate() const;
};
}
