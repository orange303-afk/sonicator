#pragma once
#include "core/sampler/SamplerTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
namespace sonicator {
// Chromatic playback of one selected immutable sample (tracker source). 128 voices, 2 ms attack,
// 2 s release on NoteOff, one-shot to the end of the source unless a verified loop is enabled at
// note-on. Voices keep their own retained source, so a selection change never cuts a ringing note.
// setSource/prepare/reset are message-thread operations performed while the audio callback is
// detached; render() is audio-thread only and allocates nothing.
class SampleVoicePlayer
{
public:
    static constexpr int maxVoices = 128;
    void setSource(SampleAudioPtr audio, bool loopAvailable, int64_t loopStart, int64_t loopEnd,
                   bool pingPong);
    void clear();
    void prepare(double outputRate);
    void reset();
    void setLooping(bool enabled) { looping = enabled; }
    void render(juce::AudioBuffer<float>&, int start, int count, const juce::MidiBuffer&);
    int activeVoices() const;
private:
    static constexpr int maxSources = 8;
    struct Source
    {
        SampleAudioPtr audio;
        bool loopAvailable = false, pingPong = false;
        int64_t loopStart = 0, loopEnd = 0;
        int users = 0;
    };
    struct Voice
    {
        bool active = false, releasing = false, sustained = false, loop = false;
        int note = 0, channel = 1, source = -1, direction = 1;
        double position = 0, increment = 1;
        float envelope = 0, velocity = 0, releaseStep = 0;
    };
    std::array<Voice, maxVoices> voices;
    std::array<Source, maxSources> sources;
    std::array<bool, 16> sustain {};
    int currentSource = -1;
    double outputRate = 44100.0;
    std::atomic<bool> looping {false};
    void event(const juce::MidiMessage&);
    void renderRange(juce::AudioBuffer<float>&, int start, int count);
    void release(Voice&);
    void stopVoice(Voice&);
};
} // namespace sonicator
