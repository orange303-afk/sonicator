#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
namespace sonicator {
// Whole-module playback. A worker thread runs libxmp and fills a bounded preallocated ring; the
// audio callback only copies from that ring. Loading, seeking and stopping join the worker and
// must be performed while the audio callback is detached (the AudioEngine does this).
class TrackerSongRenderer
{
public:
    TrackerSongRenderer();
    ~TrackerSongRenderer();
    TrackerSongRenderer(const TrackerSongRenderer&) = delete;
    TrackerSongRenderer& operator=(const TrackerSongRenderer&) = delete;
    bool load(const juce::File&, double sampleRate, juce::String& error);
    void unload();
    // Called from PreviewOutput::prepareToPlay. Only a real sample-rate change restarts libxmp.
    void prepare(double sampleRate);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void setLooping(bool enabled);
    void render(juce::AudioBuffer<float>&, int start, int count);
    bool hasModule() const { return loaded.load(std::memory_order_acquire); }
    bool isPlaying() const;
    // True once the module reached its end and the ring has drained. AudioEngine uses this to
    // detach the callback before restarting, because a restart flushes the ring.
    bool isFinished() const;
    double position() const;
    double length() const { return duration.load(std::memory_order_relaxed); }
    int underruns() const;
    juce::String title() const { return moduleTitle; }
    juce::String formatName() const { return moduleFormat; }
    int channels() const { return moduleChannels; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<bool> loaded {false};
    std::atomic<double> duration {0};
    juce::String moduleTitle, moduleFormat;
    int moduleChannels = 0;
};
} // namespace sonicator
