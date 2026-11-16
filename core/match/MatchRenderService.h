#pragma once
#include "OfflineMatchRenderer.h"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace sonicator
{
// One persistent worker renders the latest Match request. submit() never waits for a previous
// render: a newer request replaces the pending one, and the running generation is cancelled at
// the next block. The owner polls state() from the message thread (the editor timer).
class MatchRenderService
{
public:
    enum class Status { idle, preparing, ready, blocked, failed, cancelled };

    struct State
    {
        Status status = Status::idle;
        juce::uint64 generation = 0;
        juce::String key;
        juce::File file;
        MatchPlan plan;
        juce::String error;
        bool isReadyFor (const juce::String& wantedKey) const
        {
            return status == Status::ready && key == wantedKey && file.existsAsFile();
        }
    };

    using RenderFunction = std::function<MatchRenderResult (const MatchRenderRequest&, CancelCheck)>;

    explicit MatchRenderService (RenderFunction renderer = {});
    ~MatchRenderService();

    // Starts (or replaces) the request and returns its generation. Does not join the worker.
    juce::uint64 submit (MatchRenderRequest request);
    // A request that cannot even produce a plan (missing metadata, mode mismatch) still gets a
    // generation and a blocked state, so the UI can show the reason without a render.
    juce::uint64 submitBlocked (juce::String key, MatchPlan plan, juce::String reason);
    // Drops the pending request and asks the running render to stop between blocks.
    void cancel();
    State poll() const;
    bool waitUntilFinished (int timeoutMs) const;

private:
    struct Pending
    {
        MatchRenderRequest request;
        juce::uint64 generation = 0;
    };
    mutable std::mutex mutex;
    mutable std::condition_variable wake, finished;
    State current;
    std::optional<Pending> pending;
    bool stopping = false;
    std::atomic<juce::uint64> desiredGeneration { 0 };
    std::atomic<bool> cancelRequested { false };
    std::thread worker;
    RenderFunction renderer;
    void run();
};

// Collapses rapid host tempo changes: only the last request that stays put for debounceMs is
// submitted. The editor calls tick() from its existing timer.
class MatchRequestScheduler
{
public:
    explicit MatchRequestScheduler (MatchRenderService& service, int debounceMs = 250);

    // nowMs is a monotonic millisecond clock; tests pass their own, the UI passes the JUCE one.
    void request (MatchRenderRequest request, juce::uint64 nowMs);
    void requestBlocked (juce::String key, MatchPlan plan, juce::String reason);
    // Submits the pending request once it has been stable for the debounce interval.
    void tick (juce::uint64 nowMs);
    void cancel();

private:
    MatchRenderService& service;
    int debounceMs;
    std::optional<MatchRenderRequest> pending;
    juce::String blockedKey, blockedReason;
    MatchPlan blockedPlan;
    bool blockedWaiting = false;
    juce::uint64 pendingSince = 0;
    bool hasPending = false;
};
} // namespace sonicator
