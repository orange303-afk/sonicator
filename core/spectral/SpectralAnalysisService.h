#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include "core/spectral/SpectrogramCache.h"

namespace sonicator
{
// Precomputes the spectrogram cache for a list of files on background threads. Same artifact the
// preview writes on first view, so a finished run makes every one of those files open instantly.
// One run at a time; the message thread only reads atomics and polls the summary.
class SpectralAnalysisService
{
public:
    struct Progress
    {
        bool running = false;
        int done = 0, total = 0, skipped = 0, failed = 0;
        juce::String current;
        juce::int64 bytes = 0;
    };

    explicit SpectralAnalysisService(juce::File cacheFolder = defaultSpectrumCacheFolder());
    ~SpectralAnalysisService();
    SpectralAnalysisService(const SpectralAnalysisService&) = delete;
    SpectralAnalysisService& operator=(const SpectralAnalysisService&) = delete;

    void start(std::vector<juce::File> files);
    void cancel();
    bool isRunning() const { return running.load(); }
    Progress progress() const;
    // True once after a run ends, with the final counts.
    bool takeFinished(Progress& summary);

private:
    void run(std::vector<juce::File> files);
    juce::File folder;
    std::thread worker;
    std::atomic<bool> running { false }, cancelling { false };
    std::atomic<int> done { 0 }, total { 0 }, skipped { 0 }, failed { 0 };
    mutable std::mutex mutex;
    juce::String current;
    bool finished = false;
    Progress lastSummary;
};
} // namespace sonicator
