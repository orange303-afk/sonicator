#include "core/match/MatchRenderService.h"

namespace sonicator
{
MatchRenderService::MatchRenderService (RenderFunction renderFunction)
    : renderer (renderFunction ? std::move (renderFunction) : RenderFunction (
        [] (const MatchRenderRequest& request, CancelCheck cancel)
        {
#if SONICATOR_ENABLE_MATCH
            return renderMatch (request, std::move (cancel));
#else
            juce::ignoreUnused (request, cancel);
            MatchRenderResult unavailable;
            unavailable.error = "Match is not built into this application build";
            return unavailable;
#endif
        }))
{
    worker = std::thread ([this] { run(); });
}

MatchRenderService::~MatchRenderService()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        stopping = true;
        pending.reset();
    }
    cancelRequested.store (true, std::memory_order_release);
    wake.notify_all();
    if (worker.joinable()) worker.join();
}

juce::uint64 MatchRenderService::submit (MatchRenderRequest request)
{
    juce::uint64 generation = 0;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        generation = ++desiredGeneration;
        cancelRequested.store (false, std::memory_order_release);
        pending = Pending { std::move (request), generation };
        current.status = Status::preparing;
        current.generation = generation;
        current.key = pending->request.key;
        current.file = juce::File();
        current.plan = pending->request.plan;
        current.error.clear();
    }
    wake.notify_all();
    return generation;
}

juce::uint64 MatchRenderService::submitBlocked (juce::String key, MatchPlan plan, juce::String reason)
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto generation = ++desiredGeneration;
    cancelRequested.store (true, std::memory_order_release);
    pending.reset();
    current.status = Status::blocked;
    current.generation = generation;
    current.key = std::move (key);
    current.file = juce::File();
    current.plan = plan;
    current.error = std::move (reason);
    finished.notify_all();
    return generation;
}

void MatchRenderService::cancel()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        pending.reset();
        ++desiredGeneration;
        current.status = Status::cancelled;
        current.generation = desiredGeneration.load();
        current.file = juce::File();
        current.error.clear();
    }
    cancelRequested.store (true, std::memory_order_release);
    wake.notify_all();
    finished.notify_all();
}

MatchRenderService::State MatchRenderService::poll() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return current;
}

bool MatchRenderService::waitUntilFinished (int timeoutMs) const
{
    std::unique_lock<std::mutex> lock (mutex);
    return finished.wait_for (lock, std::chrono::milliseconds (timeoutMs),
                              [this] { return current.status != Status::preparing; });
}

void MatchRenderService::run()
{
    for (;;)
    {
        std::optional<Pending> job;
        {
            std::unique_lock<std::mutex> lock (mutex);
            wake.wait (lock, [this] { return stopping || pending.has_value(); });
            if (stopping) return;
            job = std::move (pending);
            pending.reset();
        }
        if (! job.has_value()) continue;
        if (job->request.key.isEmpty())
        {
            const std::lock_guard<std::mutex> lock (mutex);
            if (job->generation == desiredGeneration.load (std::memory_order_acquire))
            {
                current.status = Status::failed;
                current.generation = job->generation;
                current.error = "Match request has no result key";
                finished.notify_all();
            }
            continue;
        }
        if (desiredGeneration.load (std::memory_order_acquire) != job->generation) continue;

        const auto generation = job->generation;
        auto cancel = [this, generation]
        {
            return cancelRequested.load (std::memory_order_acquire)
                || desiredGeneration.load (std::memory_order_acquire) != generation;
        };
        MatchRenderResult result;
        try
        {
            result = renderer (job->request, cancel);
        }
        catch (const std::exception& e)
        {
            result.success = false;
            result.error = "Match preparation failed: " + juce::String::fromUTF8 (e.what());
        }
        catch (...)
        {
            result.success = false;
            result.error = "Match preparation failed";
        }

        const std::lock_guard<std::mutex> lock (mutex);
        // A newer request took over while this one rendered: its result belongs to nobody.
        if (generation != desiredGeneration.load (std::memory_order_acquire) || stopping) continue;
        current.generation = generation;
        current.key = job->request.key;
        current.plan = job->request.plan;
        if (result.success && result.file.existsAsFile())
        {
            current.status = Status::ready;
            current.file = result.file;
            current.error.clear();
        }
        else if (result.cancelled)
        {
            current.status = Status::cancelled;
            current.file = juce::File();
            current.error = result.error;
        }
        else
        {
            current.status = Status::failed;
            current.file = juce::File();
            current.error = result.error.isNotEmpty() ? result.error : "Match preparation failed";
        }
        finished.notify_all();
    }
}

MatchRequestScheduler::MatchRequestScheduler (MatchRenderService& owner, int debounce)
    : service (owner), debounceMs (juce::jmax (0, debounce))
{
}

void MatchRequestScheduler::request (MatchRenderRequest request, juce::uint64 nowMs)
{
    // The UI timer may re-plan a steady host tempo; the same key must not restart the debounce
    // window, or a request in a running DAW would never settle.
    if (hasPending && pending.has_value() && pending->key == request.key) return;
    pending = std::move (request);
    hasPending = true;
    blockedWaiting = false;
    pendingSince = nowMs;
}

void MatchRequestScheduler::requestBlocked (juce::String key, MatchPlan plan, juce::String reason)
{
    pending.reset();
    hasPending = false;
    blockedWaiting = true;
    // Keep copies for the scheduler and hand the originals to the service; moving both left the
    // service's blocked State with an empty key and error.
    blockedKey = key;
    blockedReason = reason;
    blockedPlan = plan;
    service.submitBlocked (std::move (key), blockedPlan, std::move (reason));
}

void MatchRequestScheduler::tick (juce::uint64 nowMs)
{
    if (blockedWaiting) blockedWaiting = false;
    if (! hasPending || ! pending.has_value()) return;
    if (nowMs < pendingSince || nowMs - pendingSince < (juce::uint64) debounceMs) return;
    auto request = std::move (*pending);
    pending.reset();
    hasPending = false;
    service.submit (std::move (request));
}

void MatchRequestScheduler::cancel()
{
    pending.reset();
    hasPending = false;
    blockedWaiting = false;
    service.cancel();
}
} // namespace sonicator
