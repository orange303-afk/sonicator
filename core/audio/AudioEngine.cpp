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
    loadedFile = juce::File(); audibleStart = -1; startAtNextPlay = true;
    source.setSource(nullptr); reader.reset();
    lastError = validateContainer(file);
    if (lastError.isNotEmpty()) { if (deviceMode) player.setSource(&output); return false; }
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
void AudioEngine::pause() { source.stop(); }
void AudioEngine::stop()
{
    source.stop(); source.setPosition(0); startAtNextPlay = true;
    if (deviceMode) player.setSource(nullptr);
    sampler.reset();
    if (deviceMode) player.setSource(&output);
}
void AudioEngine::seek(double seconds)
{
    if (std::isfinite(seconds)) { source.setPosition(juce::jlimit(0.0, length(), seconds)); startAtNextPlay = false; }
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
    if (enabled && loadedFile != juce::File()) lastError = sampler.load(loadedFile);
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
