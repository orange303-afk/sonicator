#pragma once
#include "CloudCache.h"
#include "CloudLayoutBuilder.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace sonicator::cloud
{
// Builds the layout off the message thread and off the audio thread. The result is published only
// for the generation that started it, so a slow build after a retag can never replace the newer
// map. A cache hit publishes immediately and skips the work.
class CloudBuildService
{
public:
    explicit CloudBuildService(juce::File cacheFolder = defaultCloudCacheFolder());
    ~CloudBuildService();
    CloudBuildService(const CloudBuildService&) = delete;
    CloudBuildService& operator=(const CloudBuildService&) = delete;

    // Replaces any running build. `input` is moved in; the caller keeps nothing.
    void start(CloudInput input);
    void cancel();
    bool isRunning() const { return running.load(); }
    float progress() const { return progressValue.load(); }
    // Message-thread poll: returns the finished layout once and clears it.
    std::optional<CloudLayout> poll();
    const juce::File& cacheFolder() const { return folder; }

private:
    void join();
    juce::File folder;
    std::thread worker;
    std::atomic<bool> cancelled { false };
    std::atomic<bool> running { false };
    std::atomic<float> progressValue { 0.0f };
    std::mutex mutex;
    std::optional<CloudLayout> result;
    uint64_t generation = 0;
};
}
