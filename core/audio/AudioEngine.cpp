#include "AudioEngine.h"
#include "ContainerValidation.h"
#include "core/legacy/LegacyFormatProbe.h"
#include <algorithm>
#include <cmath>
namespace sonicator {
AudioEngine::AudioEngine(bool openDevice) : deviceMode(openDevice)
{
    formats.registerBasicFormats();
    if (deviceMode)
    {
        readAhead.startThread();
        lastError = devices.initialiseWithDefaultDevices(0,2);
        player.setSource(&output);
        devices.addAudioCallback(&player);
    }
    source.setGain(0.7f);
}
AudioEngine::~AudioEngine()
{
    devices.removeMidiInputDeviceCallback({},&output.collector);
    devices.removeAudioCallback(&player);
    player.setSource(nullptr);
    source.setSource(nullptr);
    reader.reset();
    readAhead.stopThread(-1);
    devices.closeAudioDevice();
}
void AudioEngine::preparePreviewBlock(int blockSize)
{
    output.prepareToPlay(blockSize, deviceSampleRate());
}
void AudioEngine::renderPreviewBlock(juce::AudioBuffer<float>& buffer, int numSamples)
{
    output.getNextAudioBlock({ &buffer, 0, numSamples });
}
void AudioEngine::attach()
{
    source.setSource(reader.get(), deviceMode ? 32768 : 0, deviceMode ? &readAhead : nullptr, inputRate);
}
bool AudioEngine::loadFile(const juce::File& file)
{
    const bool midi = isMidiPreview();
    // Transport stop needs the still-connected callback to acknowledge its fade-out.
    source.stop();
    if (deviceMode) player.setSource(nullptr);
    sampler.clear();
    clearContainers();
    loadedFile = juce::File(); audibleStart = -1; startAtNextPlay = true;
    source.setSource(nullptr); reader.reset();
    lastError = validateContainer(file);
    if (lastError.isNotEmpty()) { if (deviceMode) player.setSource(&output); return false; }
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension == ".sf2")
    {
        if (deviceMode) player.setSource(&output);
        return previewSoundFont(file);
    }
    if (extension == ".rx2")
    {
        if (deviceMode) player.setSource(&output);
        return previewRex2(file);
    }
    if (extension == ".exs" || extension == ".nki")
    {
        if (deviceMode) player.setSource(&output);
        return previewFileInstrument(file);
    }
    if (isLegacyTrackerExtension(file))
    {
        if (deviceMode) player.setSource(&output);
        return previewTracker(file);
    }
    std::unique_ptr<juce::AudioFormatReader> decoded(formats.createReaderFor(file));
    if (! decoded || decoded->sampleRate <= 0 || decoded->lengthInSamples <= 0)
    {
        lastError = "Cannot decode " + file.getFileName(); if (deviceMode) player.setSource(&output); return false;
    }
    inputRate = decoded->sampleRate;
    reader = std::make_unique<juce::AudioFormatReaderSource>(decoded.release(), true);
    reader->setLooping(looping);
    attach();
    loadedFile = file;
    lastError.clear();
    if (midi) lastError = sampler.load(file);
    if (deviceMode) player.setSource(&output);
    if (lastError.isNotEmpty()) return false;
    return true;
}
void AudioEngine::play()
{
    if (trackerMode)
    {
        if (trackerSampleChosen) { sf2Audition.play(looping); return; }
        // Restarting after the end flushes the renderer ring, so the consumer must be detached.
        if (trackerSong.isFinished())
        {
            if (deviceMode) player.setSource(nullptr);
            trackerSong.stop();
            trackerSong.play();
            if (deviceMode) player.setSource(&output);
        }
        else trackerSong.play();
        return;
    }
    if (auditionMode()) { sf2Audition.play(looping); return; }
    if (rexMode) { rexSampler.startAudition(looping); return; }
    if (! reader) return;
    if (source.hasStreamFinished() || startAtNextPlay)
    {
        if (skipSilence && audibleStart < 0)
        {
            std::unique_ptr<juce::AudioFormatReader> decoded(formats.createReaderFor(loadedFile));
            audibleStart = decoded ? firstAudibleFrame(*decoded)/inputRate : 0;
        }
        source.setPosition(skipSilence ? juce::jmax(0.0,audibleStart) : 0);
        startAtNextPlay = false;
    }
    source.start();
}
void AudioEngine::clearContainers()
{
    sf2Mode = false;
    output.sf2Mode = false;
    sf2Sampler.reset();
    sf2Bank.clear();
    sf2Samples.clear();
    sf2SampleId = invalidSampleId;
    sf2Sample.reset();
    sf2Audition.clear();
    externalMode = false;
    output.externalMode = false;
    externalSampler.reset();
    externalInstrument = {};
    externalSampleId = invalidSampleId;
    externalSample.reset();
    rexMode = false;
    output.rexMode = false;
    rexSampler.reset();
    trackerMode = false;
    trackerSampleChosen = false;
    output.trackerSongMode = false;
    output.trackerSampleMidi = false;
    trackerSong.unload();
    trackerDocument.reset();
    trackerSampleIdValue = invalidSampleId;
    trackerSample.reset();
    discMode = false;
    output.discMidi = false;
    discSample.reset();
    sampleVoices.clear();
}
double AudioEngine::deviceSampleRate() const
{
    if (deviceMode)
        if (auto* device = devices.getCurrentAudioDevice())
            return device->getCurrentSampleRate();
    return 44100.0;
}
juce::String AudioEngine::soundFontPresetLabel(int index) const
{
    if (index < 0 || index >= (int) sf2Bank.presets().size()) return {};
    const auto& preset = sf2Bank.presets()[(size_t) index];
    return juce::String(preset.bank) + ":" + juce::String(preset.program) + "  " + preset.name;
}
bool AudioEngine::selectSoundFontPreset(int index)
{
    if (! sf2Mode) return false;
    if (deviceMode) player.setSource(nullptr);
    juce::String error;
    const bool ok = sf2Sampler.load(sf2Bank, index, error);
    if (! ok) lastError = error;
    else
    {
        sf2Sampler.prepare(deviceSampleRate());
        output.sf2Mode = true;
        lastError.clear();
        ++sf2Generation;
        sf2Sampler.setGeneration(sf2Generation);
        refreshSoundFontSamples(true);
    }
    if (deviceMode) player.setSource(&output);
    return ok;
}
std::vector<SampleDescriptor> AudioEngine::soundFontSamples(juce::String& error) const
{
    error.clear();
    if (! sf2Mode) { error = "SoundFont preset is not loaded"; return {}; }
    return sf2Samples;
}
bool AudioEngine::refreshSoundFontSamples(bool selectFirstPlayable)
{
    sf2Samples.clear();
    sf2SampleId = invalidSampleId;
    sf2Sample.reset();
    sf2Audition.clear();
    juce::String error;
    sf2Samples = sf2Bank.usedSamples(sf2Sampler.presetIndex(), error);
    if (! error.isEmpty()) { lastError = error; return false; }
    if (! selectFirstPlayable) return true;
    const auto first = std::find_if(sf2Samples.begin(), sf2Samples.end(),
        [] (const SampleDescriptor& descriptor) { return descriptor.isPlayable(); });
    if (first != sf2Samples.end()) return selectSoundFontSample(first->id);
    if (! sf2Samples.empty()) lastError = sf2Samples.front().error;
    else lastError = "SoundFont preset has no usable samples";
    return false;
}
bool AudioEngine::selectSoundFontSample(SampleId id)
{
    if (! sf2Mode) { lastError = "SoundFont preset is not loaded"; return false; }
    const auto found = std::find_if(sf2Samples.begin(), sf2Samples.end(),
        [id] (const SampleDescriptor& descriptor) { return descriptor.id == id; });
    if (found == sf2Samples.end()) { lastError = "SoundFont sample is not part of this preset"; return false; }
    if (! found->isPlayable()) { lastError = found->error; return false; }
    juce::String error;
    auto audio = sf2Bank.sampleAudio(id, error);
    if (audio == nullptr) { lastError = error.isNotEmpty() ? error : "Cannot decode SoundFont sample"; return false; }
    {
        // Publish only the audition source. Reattaching PreviewOutput calls prepareToPlay(),
        // which resets every MIDI voice (including the note that caused MIDI-follow).
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.setAudio(audio);
        sf2Audition.prepare(deviceSampleRate());
    }
    sf2SampleId = id;
    sf2Sample = std::move(audio);
    lastError.clear();
    return true;
}
bool AudioEngine::previewFileInstrument(const juce::File& file)
{
    if (deviceMode) player.setSource(nullptr);
    clearContainers();
    const auto extension = file.getFileExtension().toLowerCase();
    const auto format = extension == ".exs" ? FileInstrumentFormat::exs24 : FileInstrumentFormat::nki;
    juce::String error;
    if (! loadFileInstrument(file, format, externalInstrument, error))
    {
        externalInstrument = {};
        lastError = error;
        if (deviceMode) player.setSource(&output);
        return false;
    }
    if (! externalSampler.load(externalInstrument, error))
    {
        externalInstrument = {};
        lastError = error;
        if (deviceMode) player.setSource(&output);
        return false;
    }
    externalSampler.prepare(deviceSampleRate());
    externalMode = true;
    output.externalMode = true;
    ++externalGeneration;
    externalSampler.setGeneration(externalGeneration);
    loadedFile = file;
    lastError.clear();
    refreshExternalSamples(true);
    if (! isMidiPreview()) setMidiPreview(true);
    if (deviceMode) player.setSource(&output);
    return true;
}
bool AudioEngine::refreshExternalSamples(bool selectFirstPlayable)
{
    externalSampleId = invalidSampleId;
    externalSample.reset();
    sf2Audition.clear();
    if (! selectFirstPlayable) return true;
    const auto first = std::find_if(externalInstrument.sources.begin(), externalInstrument.sources.end(),
        [] (const SampleDescriptor& descriptor) { return descriptor.isPlayable(); });
    if (first != externalInstrument.sources.end()) return selectExternalSample(first->id);
    lastError = externalInstrument.sources.empty() ? "Instrument has no samples"
                                                   : "Instrument has no usable samples";
    return false;
}
bool AudioEngine::selectExternalSample(SampleId id)
{
    if (! externalMode) { lastError = "Instrument is not loaded"; return false; }
    if (id < 0 || id >= (int) externalInstrument.sources.size())
    { lastError = "Instrument sample index is out of range"; return false; }
    const auto& descriptor = externalInstrument.sources[(size_t) id];
    if (! descriptor.isPlayable())
    {
        lastError = descriptor.error.isNotEmpty() ? descriptor.error : "Instrument sample is unavailable";
        return false;
    }
    if (id >= (int) externalInstrument.audio.size() || externalInstrument.audio[(size_t) id] == nullptr)
    { lastError = "Instrument sample has no decoded audio"; return false; }
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.setAudio(externalInstrument.audio[(size_t) id]);
        sf2Audition.prepare(deviceSampleRate());
    }
    externalSampleId = id;
    externalSample = externalInstrument.audio[(size_t) id];
    lastError.clear();
    return true;
}
bool AudioEngine::previewTracker(const juce::File& file)
{
    if (deviceMode) player.setSource(nullptr);
    clearContainers();
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    if (document == nullptr)
    {
        lastError = error.isNotEmpty() ? error : "Cannot open this tracker module";
        if (deviceMode) player.setSource(&output);
        return false;
    }
    if (! trackerSong.load(file, deviceSampleRate(), error))
    {
        lastError = error.isNotEmpty() ? error : "Cannot start the tracker player";
        if (deviceMode) player.setSource(&output);
        return false;
    }
    trackerDocument = std::move(document);
    trackerSong.setLooping(looping);
    trackerMode = true;
    trackerSampleChosen = false;
    trackerSampleIdValue = invalidSampleId;
    output.trackerSongMode = true;
    output.trackerSampleMidi = false;
    ++trackerGenerationValue;
    loadedFile = file;
    lastError.clear();
    if (deviceMode) player.setSource(&output);
    return true;
}

const TrackerDocumentInfo& AudioEngine::trackerInfo() const
{
    static const TrackerDocumentInfo empty;
    return trackerDocument != nullptr ? trackerDocument->info() : empty;
}

const std::vector<TrackerSampleSlot>& AudioEngine::trackerSlots() const
{
    static const std::vector<TrackerSampleSlot> empty;
    return trackerDocument != nullptr ? trackerDocument->slots() : empty;
}

bool AudioEngine::selectTrackerSample(SampleId id)
{
    if (! trackerMode || trackerDocument == nullptr)
    { lastError = "No tracker module is loaded"; return false; }
    const auto& slots = trackerDocument->slots();
    if (id < 0 || id >= (int) slots.size()) { lastError = "Sample slot is out of range"; return false; }
    const auto& slot = slots[(size_t) id];
    if (slot.empty) { lastError = "Sample slot is empty"; return false; }
    if (slot.fmInstrument) { lastError = "AdLib instrument has no PCM sample"; return false; }
    if (slot.error.isNotEmpty()) { lastError = slot.error; return false; }
    juce::String error;
    auto audio = trackerDocument->sampleAudio((int) id, error);
    if (audio == nullptr)
    { lastError = error.isNotEmpty() ? error : "Cannot decode this sample"; return false; }
    // The song stops here; its position is kept by the renderer so returning to it is explicit.
    trackerSong.pause();
    discMode = false;
    output.discMidi = false;
    discSample.reset();
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.setAudio(audio);
        sf2Audition.prepare(deviceSampleRate());
        if (slot.looping) sf2Audition.setLoopRange(slot.loopStart, slot.loopEnd, slot.pingPong);
        sampleVoices.setSource(audio, slot.looping, slot.loopStart, slot.loopEnd, slot.pingPong);
    }
    trackerSample = audio;
    trackerSampleIdValue = id;
    trackerSampleChosen = true;
    output.trackerSongMode = false;
    output.trackerSampleMidi = true;
    ++trackerGenerationValue;
    lastError.clear();
    return true;
}

bool AudioEngine::selectTrackerWholeSong()
{    if (! trackerMode) { lastError = "No tracker module is loaded"; return false; }
    trackerSampleChosen = false;
    trackerSampleIdValue = invalidSampleId;
    trackerSample.reset();
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.stop();
        sf2Audition.clear();
        sampleVoices.reset();
    }
    output.trackerSongMode = true;
    output.trackerSampleMidi = false;
    ++trackerGenerationValue;
    lastError.clear();
    return true;
}

bool AudioEngine::setDiscSample(const SampleAudioPtr& audio, bool looping, int64_t loopStart,
                                int64_t loopEnd, bool pingPong)
{
    if (audio == nullptr || audio->frames <= 0 || audio->sampleRate <= 0)
    {
        lastError = "The decoded disc sample is empty";
        return false;
    }
    if (deviceMode) player.setSource(nullptr);
    clearContainers();
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.setAudio(audio);
        sf2Audition.prepare(deviceSampleRate());
        if (looping) sf2Audition.setLoopRange(loopStart, loopEnd, pingPong);
        sampleVoices.setSource(audio, looping, loopStart, loopEnd, pingPong);
    }
    discMode = true;
    discSample = audio;
    ++discGenerationValue;
    output.discMidi = true;
    lastError.clear();
    if (deviceMode) player.setSource(&output);
    return true;
}

void AudioEngine::clearDiscSample()
{
    if (deviceMode) player.setSource(nullptr);
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.stop();
        sf2Audition.clear();
        sampleVoices.reset();
        sampleVoices.clear();
    }
    discMode = false;
    output.discMidi = false;
    discSample.reset();
    ++discGenerationValue;
    lastError.clear();
    if (deviceMode) player.setSource(&output);
}

bool AudioEngine::previewRex2(const juce::File& file, int startNote)
{
    if (deviceMode) player.setSource(nullptr);
    clearContainers();
    juce::String error;
    if (! rexSampler.load(file, startNote, error))
    {
        lastError = error;
        if (deviceMode) player.setSource(&output);
        return false;
    }
    rexSampler.prepare(deviceSampleRate());
    rexMode = true;
    output.rexMode = true;
    loadedFile = file;
    lastError.clear();
    if (! isMidiPreview()) setMidiPreview(true);
    if (deviceMode) player.setSource(&output);
    return true;
}
bool AudioEngine::previewSoundFont(const juce::File& file, int presetIndex)
{
    if (deviceMode) player.setSource(nullptr);
    clearContainers();
    juce::String error;
    if (! sf2Bank.load(file, error))
    {
        lastError = error;
        if (deviceMode) player.setSource(&output);
        return false;
    }
    int index = presetIndex;
    if (index < 0)
    {
        index = sf2Bank.firstDrumPreset();
        if (index < 0) index = 0; // a bank without a drum kit opens its first melodic preset
    }
    if (index >= (int) sf2Bank.presets().size() || ! sf2Sampler.load(sf2Bank, index, error))
    {
        lastError = error.isNotEmpty() ? error : "Cannot load SoundFont preset";
        if (deviceMode) player.setSource(&output);
        return false;
    }
    sf2Sampler.prepare(deviceSampleRate());
    sf2Mode = true;
    output.sf2Mode = true;
    ++sf2Generation;
    sf2Sampler.setGeneration(sf2Generation);
    loadedFile = file;
    lastError.clear();
    refreshSoundFontSamples(true);
    if (! isMidiPreview()) setMidiPreview(true);
    if (deviceMode) player.setSource(&output);
    return true;
}
void AudioEngine::pause()
{
    if (trackerMode)
    {
        if (trackerSampleChosen) sf2Audition.pause();
        else trackerSong.pause();
        return;
    }
    if (auditionMode()) { sf2Audition.pause(); return; }
    if (rexMode) { rexSampler.pauseAudition(); return; }
    source.stop();
}
void AudioEngine::stop()
{
    if (auditionMode()) sf2Audition.stop();
    if (rexMode) rexSampler.stopAudition();
    if (trackerMode && trackerSampleChosen) sf2Audition.stop();
    source.stop(); source.setPosition(0); startAtNextPlay = true;
    if (deviceMode) player.setSource(nullptr);
    sampler.reset();
    sf2Sampler.reset();
    externalSampler.reset();
    rexSampler.reset();
    sampleVoices.reset();
    // Stop always returns the composition to its beginning, including when a sample is selected.
    if (trackerMode) trackerSong.stop();
    if (deviceMode) player.setSource(&output);
}
void AudioEngine::seek(double seconds)
{
    if (! std::isfinite(seconds)) return;
    if (trackerMode)
    {
        if (trackerSampleChosen) { sf2Audition.seek(juce::jlimit(0.0, length(), seconds)); return; }
        const double target = juce::jlimit(0.0, length(), seconds);
        // The renderer flushes its ring, so the consumer must be detached while it happens.
        if (deviceMode) player.setSource(nullptr);
        trackerSong.seek(target);
        if (deviceMode) player.setSource(&output);
        return;
    }
    if (auditionMode()) { sf2Audition.seek(juce::jlimit(0.0, length(), seconds)); return; }
    if (rexMode) { rexSampler.seekAudition(juce::jlimit(0.0, length(), seconds)); return; }
    source.setPosition(juce::jlimit(0.0, length(), seconds)); startAtNextPlay = false;
}
void AudioEngine::setVolume(float gain)
{
    source.setGain(std::isfinite(gain) ? juce::jlimit(0.0f,1.0f,gain) : 0.0f);
    output.gain = source.getGain();
}
void AudioEngine::setLooping(bool enabled)
{
    if (looping == enabled) return;
    looping = enabled;
    sampler.loop = enabled;
    sampleVoices.setLooping(enabled);
    if (trackerMode)
    {
        if (trackerSampleChosen) sf2Audition.setLooping(enabled);
        else trackerSong.setLooping(enabled);
        return;
    }
    if (auditionMode()) { sf2Audition.setLooping(enabled); return; }
    if (rexMode) { rexSampler.setAuditionLoop(enabled); return; }
    if (! reader) return;
    auto positionBefore = position(); bool resume = isPlaying();
    source.stop(); source.setSource(nullptr);
    reader->setLooping(looping); // read-ahead is detached before mutating its source.
    attach(); seek(positionBefore);
    if (resume) play();
}
bool AudioEngine::setMidiPreview(bool enabled)
{
    if (enabled == isMidiPreview()) return true;
    source.stop(); startAtNextPlay = true;
    if (deviceMode) player.setSource(nullptr);
    sampler.clear(); sampleVoices.reset(); lastError.clear();
    if (enabled && loadedFile != juce::File() && ! sf2Mode && ! externalMode && ! rexMode
        && ! trackerMode && ! discMode) lastError = sampler.load(loadedFile);
    output.midi = enabled && lastError.isEmpty();
    if (deviceMode)
    {
        if (enabled && ! midiInputsConnected)
        {
            for (const auto& input : juce::MidiInput::getAvailableDevices())
                devices.setMidiInputDeviceEnabled(input.identifier,true);
            devices.addMidiInputDeviceCallback({},&output.collector); midiInputsConnected = true;
        }
        player.setSource(&output);
    }
    return lastError.isEmpty();
}
}
