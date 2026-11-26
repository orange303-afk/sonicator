#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include "core/dsp/Spectrogram.h"

namespace sonicator
{
// One request the preview panel makes for the card it is about to draw. `key` identifies the
// exact picture (source + range + size + parameters), so the caller can tell a stale result
// from the one it is waiting for without comparing every field.
struct SpectrogramRequest
{
    juce::File file;          // ordinary file; read through AudioFormatManager on the worker
    SampleAudioPtr audio;     // decoded snapshot for containers, edits and sampler sources
    juce::String key;
    double startSeconds = 0.0, endSeconds = 0.0;
    SpectrogramParams params;
    // Whole-file overviews are written to the persistent cache by the worker; a zoomed detail
    // pass never touches the disk.
    juce::File cacheSource;
    bool cacheOverview = false;
};

// Latest-wins background worker for one preview card. It never touches the audio device, the
// library or the file the preview streams from: a request either reads its own file reader or
// an immutable SampleAudio snapshot. Computing never blocks the message thread, and a cancelled
// or superseded request publishes nothing.
class SpectrogramService
{
public:
    enum class Phase { idle, computing, ready, failed };
    struct State
    {
        Phase phase = Phase::idle;
        std::shared_ptr<const SpectrogramGrid> grid;
        juce::String key;   // the request key this state belongs to
        juce::String error; // human-readable reason when phase == failed
    };

    explicit SpectrogramService(juce::File cacheFolder = {});
    ~SpectrogramService();
    SpectrogramService(const SpectrogramService&) = delete;
    SpectrogramService& operator=(const SpectrogramService&) = delete;

    void prepare(SpectrogramRequest);
    void cancel();
    State state() const;

    // Identity of the shown file: path, size and mtime, so a rewritten file never reuses the
    // previous spectrum.
    static juce::String sourceKey(const juce::File& file);

private:
    void run();
    void publish(Phase, std::shared_ptr<const SpectrogramGrid>, const juce::String& key,
                 const juce::String& error);

    mutable std::mutex lock;
    std::condition_variable wake;
    std::optional<SpectrogramRequest> pending;
    std::atomic<std::uint64_t> generation { 0 };
    State current;
    juce::File cacheFolder;
    std::thread worker;
    bool stopping = false;
};
} // namespace sonicator
