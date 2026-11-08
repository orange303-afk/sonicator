#pragma once
#include "core/sampler/SamplerTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
namespace sonicator {
// Plays one immutable decoded source as-is (no transpose, no MIDI mapping). Publication and
// prepare are message-thread operations performed while the output callback is detached;
// render() is audio-thread only and allocates nothing.
class SampleAudition
{
public:
    void setAudio(SampleAudioPtr);
    // Optional loop region declared by the source (frames, end exclusive). Calling this with an
    // invalid range restores the whole-source loop. setAudio always resets to the new source.
    void setLoopRange(int64_t start, int64_t end, bool pingPong);
    void clear() { setAudio({}); }
    void prepare(double outputRate);
    void play(bool looping);
    void setLooping(bool enabled) { looping = enabled; }
    void pause();
    void stop();
    void seek(double seconds);
    void render(juce::AudioBuffer<float>&, int start, int count);
    bool hasAudio() const { return source != nullptr && source->frames > 0; }
    bool isPlaying() const { return playing.load(std::memory_order_relaxed) && hasAudio(); }
    bool isLooping() const { return looping.load(std::memory_order_relaxed); }
    double position() const;
    double length() const;
    SampleAudioPtr audio() const { return source; }
private:
    SampleAudioPtr source;
    double outputRate = 44100.0;
    std::atomic<int64_t> positionFrames { 0 };
    std::atomic<bool> playing { false }, looping { false };
    int64_t loopStart = 0, loopEnd = 0;
    bool pingPong = false;
    std::atomic<int> direction { 1 };
};
} // namespace sonicator
