#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "MidiSampler.h"
#include "core/sf2/Sf2Bank.h"
#include "core/sf2/Sf2KitSampler.h"
#include "core/rex/Rex2SlicePlayer.h"
namespace sonicator {
// Control methods are message-thread only. transport() also permits offline rendering.
class AudioEngine
{
public:
    explicit AudioEngine(bool openDevice = true);
    ~AudioEngine();
    bool loadFile(const juce::File&);
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
    // REX2 slices are a MIDI drum map starting at startNote (default 36). Gate stops a slice on
    // NoteOff; one-shot lets it ring out.
    bool previewRex2(const juce::File&, int startNote = 36);
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
    bool isPlaying() const { return rexMode ? rexSampler.auditionPlaying() : source.isPlaying(); }
    bool isLooping() const { return looping; }
    float getVolume() const { return source.getGain(); }
    double position() const { return rexMode ? rexSampler.auditionSeconds() : source.getCurrentPosition(); }
    double length() const { return rexMode ? rexSampler.lengthSeconds() : source.getLengthInSeconds(); }
    juce::String error() const { return lastError; }
    juce::AudioTransportSource& transport() { return source; }
    juce::AudioDeviceManager& deviceManager() { return devices; }
    // Offline render of the same PreviewOutput used by the device callback. Tests use it to
    // verify the complete MIDI -> sampler -> mix path without an audio device.
    void preparePreviewBlock(int blockSize);
    void renderPreviewBlock(juce::AudioBuffer<float>&, int numSamples);
    MidiEventQueue& midiCollector() { return output.collector; }
private:
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
    bool sf2Mode = false;
    Rex2SlicePlayer rexSampler;
    bool rexMode = false;
    PreviewOutput output {source,sampler,sf2Sampler,rexSampler};
    juce::AudioSourcePlayer player;
    void attach();
    void clearContainers();
    double deviceSampleRate() const;
};
}
