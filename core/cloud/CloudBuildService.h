#pragma once
#include "CloudCache.h"
#include "CloudLayoutBuilder.h"
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace sonicator::cloud
{
// Builds the layout off the message thread and off the audio thread. The result is published only
// for the generation that started it, so a slow build after a retag can never replace the newer
// map. A cache hit publishes immediately and skips the work.
//
// Starting or cancelling never waits for a running build: this object only asks the old worker to
// stop and retires its future, so a 50k-file catalog cannot freeze the window while a newer build
// supersedes the old one. Retired workers are reaped from poll() once they have finished.
class CloudBuildService
{
public:
    explicit CloudBuildService(juce::File cacheFolder = defaultCloudCacheFolder());
    ~CloudBuildService();
    CloudBuildService(const CloudBuildService&) = delete;
    CloudBuildService& operator=(const CloudBuildService&) = delete;

    // The application entry point: the worker opens its own catalog connection, loads the tagged
    // files and builds, so neither the batch read nor the layout ever runs on the message thread.
    void startCatalog(const juce::File& databasePath);
    // Replaces any running build. `input` is moved in; the caller keeps nothing.
    void start(CloudInput input);
    void cancel();
    bool isRunning() const { return running.load(); }
    float progress() const { return progressValue.load(); }
    // Message-thread poll: returns the finished layout once, clears it and reaps finished workers.
    std::optional<CloudLayout> poll();
    const juce::File& cacheFolder() const { return folder; }

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;
    uint64_t nextGeneration();
    void finishFor(uint64_t generationStarted);
    void publish(uint64_t generationStarted, const CancelFlag& flag, CloudLayout layout);
    void retireFinishedWork();
    void retireCurrentWork();
    void waitForWorkers();
    void runCatalog(uint64_t generationStarted, CancelFlag flag, juce::File databasePath);
    void runBuild(uint64_t generationStarted, CancelFlag flag, CloudInput input);

    juce::File folder;
    // The current build and the superseded ones that have not stopped yet. A std::future's
    // destructor waits for its task, so a future is only ever destroyed after it reports ready.
    std::future<void> work;
    std::vector<std::future<void>> retired;
    CancelFlag activeCancel;
    std::atomic<bool> running { false };
    std::atomic<float> progressValue { 0.0f };
    std::mutex mutex;
    std::optional<CloudLayout> result;
    uint64_t generation = 0;
};
} // namespace sonicator::cloud
