#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace sonicator
{
// The sound a hovered point makes. It is deliberately not the preview transport: hovering must
// never touch the selected file, its waveform edits, the undo history or the play position. One
// short clip, its own gain and a 10 ms ramp on both ends, so a fast mouse never clicks.
class HoverAudition
{
public:
    struct Clip
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0;
        int64_t frames = 0;
    };
    using ClipPtr = std::shared_ptr<const Clip>;

    // Message-thread calls. The audio thread only ever calls render().
    void setAudio(ClipPtr);
    void clear();
    void play();
    void stop(); // fades out; the clip is released when the ramp reaches zero
    void prepare(double outputRate);

    void render(juce::AudioBuffer<float>& out, int start, int count);

    bool hasAudio() const { return clip != nullptr && clip->frames > 0; }
    bool isPlaying() const { return playing.load(std::memory_order_relaxed) && hasAudio(); }
    double position() const;
    double length() const;
    float currentGain() const { return gain.load(std::memory_order_relaxed); }

    // Bounded first seconds of a file through the shared analysis decoder. Empty on failure, so a
    // format the decoder cannot open stays silent instead of pretending to play.
    static ClipPtr load(const juce::File&, double maxSeconds = 10.0);

private:
    ClipPtr clip;
    double outputRate = 44100.0;
    std::atomic<double> positionFrames { 0 };
    std::atomic<bool> playing { false };
    std::atomic<bool> fadingOut { false };
    std::atomic<float> gain { 0.0f };
};
}
