#include "AudioEngine.h"
#include "ContainerValidation.h"
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
    }
    if (deviceMode) player.setSource(&output);
    return ok;
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
    loadedFile = file;
    lastError.clear();
    if (! isMidiPreview()) setMidiPreview(true);
    if (deviceMode) player.setSource(&output);
    return true;
}
void AudioEngine::pause()
{
    if (rexMode) { rexSampler.pauseAudition(); return; }
    source.stop();
}
void AudioEngine::stop()
{
    if (rexMode) rexSampler.stopAudition();
    source.stop(); source.setPosition(0); startAtNextPlay = true;
    if (deviceMode) player.setSource(nullptr);
    sampler.reset();
    sf2Sampler.reset();
    rexSampler.reset();
    if (deviceMode) player.setSource(&output);
}
void AudioEngine::seek(double seconds)
{
    if (! std::isfinite(seconds)) return;
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
    if (enabled && loadedFile != juce::File() && ! sf2Mode && ! rexMode) lastError = sampler.load(loadedFile);
    output.midi = enabled && lastError.isEmpty();
    if (deviceMode)
    {
        if (enabled && ! midiInputsConnected)
        {
            for (const auto& input : juce::MidiInput::getAvailableDevices()) devices.setMidiInputDeviceEnabled(input.identifier,true);
            devices.addMidiInputDeviceCallback({},&output.collector); midiInputsConnected = true;
        }
        player.setSource(&output);
    }
    return lastError.isEmpty();
}
}
