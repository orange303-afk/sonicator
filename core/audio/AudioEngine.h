#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
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
    bool isPlaying() const { return source.isPlaying(); }
    bool isLooping() const { return looping; }
    float getVolume() const { return source.getGain(); }
    double position() const { return source.getCurrentPosition(); }
    double length() const { return source.getLengthInSeconds(); }
    juce::String error() const { return lastError; }
    juce::AudioTransportSource& transport() { return source; }
    juce::AudioDeviceManager& deviceManager() { return devices; }
private:
    bool deviceMode, looping = false;
    double inputRate = 0;
    juce::String lastError;
    juce::AudioFormatManager formats;
    juce::AudioDeviceManager devices;
    juce::TimeSliceThread readAhead { "Preview read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioTransportSource source;
    juce::AudioSourcePlayer player;
    void attach();
};
}
