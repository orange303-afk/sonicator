#include "AudioEngine.h"
#include <cmath>
namespace sonicator {
AudioEngine::AudioEngine(bool openDevice) : deviceMode(openDevice)
{
    formats.registerBasicFormats();
    if (deviceMode)
    {
        readAhead.startThread();
        lastError = devices.initialiseWithDefaultDevices(0,2);
        player.setSource(&source);
        devices.addAudioCallback(&player);
    }
    source.setGain(0.7f);
}
AudioEngine::~AudioEngine()
{
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
    source.stop(); source.setSource(nullptr); reader.reset();
    std::unique_ptr<juce::AudioFormatReader> decoded(formats.createReaderFor(file));
    if (! decoded || decoded->sampleRate <= 0 || decoded->lengthInSamples <= 0)
    {
        lastError = "Cannot decode " + file.getFileName(); return false;
    }
    inputRate = decoded->sampleRate;
    reader = std::make_unique<juce::AudioFormatReaderSource>(decoded.release(), true);
    reader->setLooping(looping);
    attach();
    lastError.clear();
    return true;
}
void AudioEngine::play()
{
    if (! reader) return;
    if (source.hasStreamFinished()) source.setPosition(0);
    source.start();
}
void AudioEngine::pause() { source.stop(); }
void AudioEngine::stop() { source.stop(); source.setPosition(0); }
void AudioEngine::seek(double seconds)
{
    if (std::isfinite(seconds)) source.setPosition(juce::jlimit(0.0, length(), seconds));
}
void AudioEngine::setVolume(float gain)
{
    source.setGain(std::isfinite(gain) ? juce::jlimit(0.0f,1.0f,gain) : 0.0f);
}
void AudioEngine::setLooping(bool enabled)
{
    if (looping == enabled) return;
    looping = enabled;
    if (! reader) return;
    auto positionBefore = position(); bool resume = isPlaying();
    source.stop(); source.setSource(nullptr);
    reader->setLooping(looping); // read-ahead is detached before mutating its source.
    attach(); seek(positionBefore);
    if (resume) play();
}
}
