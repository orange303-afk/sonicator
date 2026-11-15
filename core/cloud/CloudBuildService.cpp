#include "CloudBuildService.h"
#include "CloudSource.h"
#include "core/database/Database.h"

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

void CloudBuildService::startCatalog(const juce::File& databasePath)
{
    cancel();
    join();
    {
        std::lock_guard lock(mutex);
        result.reset();
    }
    cancelled = false;
    progressValue = 0.0f;
    if (! databasePath.existsAsFile())
    {
        running = false;
        return;
    }
    running = true;
    const auto current = ++generation;
    worker = std::thread([this, current, databasePath]()
    {
        auto publish = [this, current](CloudLayout layout)
        {
            std::lock_guard lock(mutex);
            if (cancelled.load() || current != generation || ! layout.valid()) return;
            result = std::move(layout);
            progressValue = 1.0f;
            running = false;
        };
        progressValue = 0.05f;
        CloudInput input;
        try
        {
            Database database(databasePath);
            SampleRepository repository(database);
            repository.requireCompatibleSchema();
            input = loadCloudInput(repository);
        }
        catch (...)
        {
            // A catalog this build must not touch, a busy file or a missing folder: no map, no
            // crash in a worker thread, and the view keeps its previous layout.
            running = false;
            return;
        }
        if (cancelled.load() || input.nodes.empty()) { running = false; return; }
        progressValue = 0.15f;
        if (auto cached = readCloudLayout(cloudCacheFile(input.fingerprint, folder), input.fingerprint))
        {
            publish(std::move(*cached));
            return;
        }
        auto layout = buildCloudLayout(input, {}, [this] { return cancelled.load(); });
        if (! layout.valid()) { running = false; return; }
        progressValue = 0.9f;
        writeCloudLayout(layout, cloudCacheFile(layout.fingerprint, folder));
        pruneCloudCache(folder);
        publish(std::move(layout));
    });
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
