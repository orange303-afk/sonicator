#include "AudioEngine.h"
#include "ContainerValidation.h"
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
    if (auditionMode()) { sf2Audition.pause(); return; }
    if (rexMode) { rexSampler.pauseAudition(); return; }
    source.stop();
}
void AudioEngine::stop()
{
    if (auditionMode()) sf2Audition.stop();
    if (rexMode) rexSampler.stopAudition();
    source.stop(); source.setPosition(0); startAtNextPlay = true;
    if (deviceMode) player.setSource(nullptr);
    sampler.reset();
    sf2Sampler.reset();
    externalSampler.reset();
    rexSampler.reset();
    if (deviceMode) player.setSource(&output);
}
void AudioEngine::seek(double seconds)
{
    if (! std::isfinite(seconds)) return;
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
    sampler.clear(); lastError.clear();
    if (enabled && loadedFile != juce::File() && ! sf2Mode && ! externalMode && ! rexMode) lastError = sampler.load(loadedFile);
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
