#include "CloudBuildService.h"
#include "CloudSource.h"
#include "core/database/Database.h"
#include <chrono>
#include <cmath>

namespace sonicator::cloud
{
CloudBuildService::CloudBuildService(juce::File cacheFolder) : folder(std::move(cacheFolder)) {}

CloudBuildService::~CloudBuildService()
{
    waitForWorkers();
}

uint64_t CloudBuildService::nextGeneration()
{
    // Generation is read by workers under the mutex, so it is written under the mutex too.
    std::lock_guard lock(mutex);
    return ++generation;
}

void CloudBuildService::finishFor(uint64_t generationStarted)
{
    std::lock_guard lock(mutex);
    // A superseded worker must never clear the flag of the build that replaced it.
    if (generationStarted == generation) running = false;
}

void CloudBuildService::publish(uint64_t generationStarted, const CancelFlag& flag, CloudLayout layout)
{
    std::lock_guard lock(mutex);
    if (flag->load() || generationStarted != generation || ! layout.valid()) return;
    result = std::move(layout);
    progressValue = 1.0f;
    running = false;
}

void CloudBuildService::retireFinishedWork()
{
    for (auto it = retired.begin(); it != retired.end();)
    {
        if (it->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            ++it;
            continue;
        }
        try { it->get(); } catch (...) {}
        it = retired.erase(it);
    }
}

void CloudBuildService::retireCurrentWork()
{
    if (work.valid()) retired.push_back(std::move(work));
    retireFinishedWork();
}

void CloudBuildService::waitForWorkers()
{
    // Only the destructor calls this. Cancellation is responsive now, so the wait is short, but it
    // still happens here rather than on the message thread of a running window.
    cancel();
    retireCurrentWork();
    for (auto& future : retired)
    {
        try { future.get(); } catch (...) {}
    }
    retired.clear();
}

void CloudBuildService::startCatalog(const juce::File& databasePath)
{
    // Never wait for a worker here: this runs on the message thread. The old worker gets its own
    // cancellation flag, stops on its next check and can never publish for a newer generation.
    cancel();
    retireCurrentWork();
    {
        std::lock_guard lock(mutex);
        result.reset();
    }
    progressValue = 0.0f;
    if (! databasePath.existsAsFile())
    {
        running = false;
        return;
    }
    const auto current = nextGeneration();
    auto flag = std::make_shared<std::atomic<bool>>(false);
    activeCancel = flag;
    running = true;
    try
    {
        work = std::async(std::launch::async, [this, current, flag, databasePath]
        {
            try
            {
                runCatalog(current, flag, databasePath);
            }
            catch (const std::exception& e)
            {
                finishFor(current);
                juce::Logger::writeToLog("[cloud] build failed: " + juce::String(e.what()));
            }
            catch (...)
            {
                finishFor(current);
                juce::Logger::writeToLog("[cloud] build failed with an unknown error");
            }
        });
    }
    catch (const std::exception& e)
    {
        running = false;
        juce::Logger::writeToLog("[cloud] cannot start the build worker: " + juce::String(e.what()));
    }
}

void CloudBuildService::start(CloudInput input)
{
    cancel();
    retireCurrentWork();
    {
        std::lock_guard lock(mutex);
        result.reset();
    }
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
    const auto current = nextGeneration();
    auto flag = std::make_shared<std::atomic<bool>>(false);
    activeCancel = flag;
    running = true;
    try
    {
        work = std::async(std::launch::async, [this, current, flag, input = std::move(input)]() mutable
        {
            try
            {
                runBuild(current, flag, std::move(input));
            }
            catch (const std::exception& e)
            {
                finishFor(current);
                juce::Logger::writeToLog("[cloud] build failed: " + juce::String(e.what()));
            }
            catch (...)
            {
                finishFor(current);
                juce::Logger::writeToLog("[cloud] build failed with an unknown error");
            }
        });
    }
    catch (const std::exception& e)
    {
        running = false;
        juce::Logger::writeToLog("[cloud] cannot start the build worker: " + juce::String(e.what()));
    }
}

void CloudBuildService::cancel()
{
    if (activeCancel != nullptr) activeCancel->store(true);
    running = false;
    // Taking the mutex makes cancellation deterministic: once cancel() returns, a worker that was
    // about to publish finds the flag or loses the race to this reset, and the stale map is gone.
    std::lock_guard lock(mutex);
    result.reset();
}

std::optional<CloudLayout> CloudBuildService::poll()
{
    retireFinishedWork();
    std::lock_guard lock(mutex);
    auto out = std::move(result);
    result.reset();
    return out;
}

void CloudBuildService::runCatalog(uint64_t generationStarted, CancelFlag flag, juce::File databasePath)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    auto cancelled = [&flag] { return flag->load(); };
    auto elapsedMs = [&started]
    {
        return juce::String((int) std::lround(juce::Time::getMillisecondCounterHiRes() - started));
    };

    CloudInput input;
    try
    {
        Database database(databasePath);
        SampleRepository repository(database);
        repository.requireCompatibleSchema();
        progressValue = 0.05f;
        input = loadCloudInput(repository, cancelled);
    }
    catch (...)
    {
        // A catalog this build must not touch, a busy file or a missing folder: no map, no crash
        // in a worker thread, and the view keeps its previous layout.
        finishFor(generationStarted);
        juce::Logger::writeToLog("[cloud] catalog read failed after " + elapsedMs() + " ms; keeping the previous map");
        return;
    }
    if (cancelled())
    {
        finishFor(generationStarted);
        juce::Logger::writeToLog("[cloud] catalog read cancelled after " + elapsedMs() + " ms");
        return;
    }
    if (input.nodes.empty())
    {
        finishFor(generationStarted);
        juce::Logger::writeToLog("[cloud] no tagged files; the map stays empty");
        return;
    }
    juce::Logger::writeToLog("[cloud] read " + juce::String((int) input.nodes.size()) + " files and "
                             + juce::String((int) input.tags.size()) + " tags in " + elapsedMs() + " ms");
    progressValue = 0.2f;

    if (auto cached = readCloudLayout(cloudCacheFile(input.fingerprint, folder), input.fingerprint))
    {
        juce::Logger::writeToLog("[cloud] cache hit after " + elapsedMs() + " ms");
        publish(generationStarted, flag, std::move(*cached));
        return;
    }

    auto layout = buildCloudLayout(input, {}, cancelled);
    if (! layout.valid())
    {
        finishFor(generationStarted);
        juce::Logger::writeToLog(cancelled() ? "[cloud] layout cancelled after " + elapsedMs() + " ms"
                                             : "[cloud] layout has no islands after " + elapsedMs() + " ms");
        return;
    }
    juce::Logger::writeToLog("[cloud] built " + juce::String((int) layout.points.size()) + " points in "
                             + elapsedMs() + " ms");
    progressValue = 0.9f;
    const auto cacheFile = cloudCacheFile(layout.fingerprint, folder);
    writeCloudLayout(layout, cacheFile);
    // The catalog tag is what lets the next launch put this map on screen before it reads anything.
    writeCloudLayoutCatalog(cacheFile, databasePath);
    pruneCloudCache(folder);
    juce::Logger::writeToLog("[cloud] cache written after " + elapsedMs() + " ms");
    publish(generationStarted, flag, std::move(layout));
}

void CloudBuildService::runBuild(uint64_t generationStarted, CancelFlag flag, CloudInput input)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    auto cancelled = [&flag] { return flag->load(); };
    progressValue = 0.1f;
    auto layout = buildCloudLayout(input, {}, cancelled);
    if (! layout.valid() || cancelled())
    {
        finishFor(generationStarted);
        return;
    }
    juce::Logger::writeToLog("[cloud] built " + juce::String((int) layout.points.size()) + " points in "
                             + juce::String((int) std::lround(juce::Time::getMillisecondCounterHiRes() - started)) + " ms");
    progressValue = 0.9f;
    writeCloudLayout(layout, cloudCacheFile(layout.fingerprint, folder));
    pruneCloudCache(folder);
    progressValue = 1.0f;
    publish(generationStarted, flag, std::move(layout));
}
} // namespace sonicator::cloud
