#include "CloudBuildService.h"

namespace sonicator::cloud
{
CloudBuildService::CloudBuildService(juce::File cacheFolder) : folder(std::move(cacheFolder)) {}

CloudBuildService::~CloudBuildService()
{
    cancel();
    join();
}

void CloudBuildService::join()
{
    if (worker.joinable()) worker.join();
}

void CloudBuildService::start(CloudInput input)
{
    cancel();
    join();
    {
        std::lock_guard lock(mutex);
        result.reset();
    }
    cancelled = false;
    progressValue = 0.0f;
    if (input.nodes.empty())
    {
        running = false;
        return;
    }
    const auto fingerprint = input.fingerprint;
    if (auto cached = readCloudLayout(cloudCacheFile(fingerprint, folder), fingerprint))
    {
        std::lock_guard lock(mutex);
        result = std::move(cached);
        progressValue = 1.0f;
        running = false;
        return;
    }
    running = true;
    const auto current = ++generation;
    worker = std::thread([this, current, input = std::move(input)]() mutable
    {
        progressValue = 0.1f;
        auto layout = buildCloudLayout(input, {}, [this] { return cancelled.load(); });
        if (! layout.valid() || cancelled.load()) return;
        progressValue = 0.9f;
        writeCloudLayout(layout, cloudCacheFile(layout.fingerprint, folder));
        pruneCloudCache(folder);
        progressValue = 1.0f;
        std::lock_guard lock(mutex);
        if (cancelled.load() || current != generation) return; // a newer build superseded this one
        result = std::move(layout);
        running = false;
    });
}

void CloudBuildService::cancel()
{
    cancelled = true;
    running = false;
    // Taking the mutex makes cancellation deterministic: once cancel() returns, a worker that was
    // about to publish finds the flag or loses the race to this reset, and the stale map is gone.
    std::lock_guard lock(mutex);
    result.reset();
}

std::optional<CloudLayout> CloudBuildService::poll()
{
    std::lock_guard lock(mutex);
    auto out = std::move(result);
    result.reset();
    return out;
}
}
