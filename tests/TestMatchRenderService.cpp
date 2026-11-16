#include <catch2/catch_test_macros.hpp>
#include "core/match/MatchRenderService.h"
#include "TestHelpers.h"
#include <condition_variable>
#include <mutex>
#include <vector>

using namespace sonicator;

namespace
{
struct RendererHarness
{
    TestDirectory directory;
    std::mutex mutex;
    std::condition_variable gate;
    std::vector<juce::String> seen;
    bool openA = false;
    bool throwOnB = false;
    std::atomic<int> current { 0 };

    MatchRenderResult operator() (const MatchRenderRequest& request, CancelCheck cancel)
    {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            seen.push_back (request.key);
        }
        if (request.key == "A")
        {
            std::unique_lock<std::mutex> lock (mutex);
            gate.wait_for (lock, std::chrono::seconds (3), [this] { return openA; });
        }
        if (request.key == "B" && throwOnB) throw std::runtime_error ("boom");
        if (cancel && cancel())
        {
            MatchRenderResult result;
            result.cancelled = true;
            result.error = "cancelled";
            return result;
        }
        MatchRenderResult result;
        result.success = true;
        result.file = directory.dir.getChildFile ("out-" + request.key + ".wav");
        result.file.replaceWithText ("audio");
        result.frames = 100;
        result.sampleRate = 44100.0;
        result.channels = 2;
        return result;
    }

    void releaseA()
    {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            openA = true;
        }
        gate.notify_all();
    }

    std::vector<juce::String> keys()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return seen;
    }
};

MatchRenderRequest requestFor (const juce::String& key)
{
    MatchRenderRequest request;
    request.key = key;
    request.source.identity = "test";
    request.plan.action = MatchAction::transform;
    request.plan.targetFrames = 100;
    return request;
}
} // namespace

TEST_CASE ("A newer request replaces a running render without stale Ready",
           "[plugin-match][service]")
{
    RendererHarness harness;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    service.submit (requestFor ("A"));
    std::this_thread::sleep_for (std::chrono::milliseconds (30));
    CHECK (service.poll().status == MatchRenderService::Status::preparing);
    service.submit (requestFor ("B"));
    harness.releaseA();
    REQUIRE (service.waitUntilFinished (3000));
    const auto state = service.poll();
    CHECK (state.status == MatchRenderService::Status::ready);
    CHECK (state.key == "B");
    CHECK (state.isReadyFor ("B"));
    CHECK_FALSE (state.isReadyFor ("A"));
    const auto seen = harness.keys();
    REQUIRE (! seen.empty());
    CHECK (seen.back() == "B");
}

TEST_CASE ("Rapid A to B to A leaves only the last key Ready", "[plugin-match][service]")
{
    RendererHarness harness;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    service.submit (requestFor ("A"));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));
    service.submit (requestFor ("B"));
    service.submit (requestFor ("A"));
    harness.releaseA();
    REQUIRE (service.waitUntilFinished (3000));
    const auto state = service.poll();
    CHECK (state.key == "A");
    CHECK (state.isReadyFor ("A"));
}

TEST_CASE ("An old ready result is not Ready for a new desired key", "[plugin-match][service]")
{
    RendererHarness harness;
    harness.openA = true;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    service.submit (requestFor ("A"));
    REQUIRE (service.waitUntilFinished (3000));
    REQUIRE (service.poll().isReadyFor ("A"));
    service.submit (requestFor ("B"));
    const auto during = service.poll();
    CHECK (during.status == MatchRenderService::Status::preparing);
    CHECK_FALSE (during.isReadyFor ("A"));
    REQUIRE (service.waitUntilFinished (3000));
    CHECK (service.poll().isReadyFor ("B"));
}

TEST_CASE ("Cancel and renderer exceptions leave the service alive", "[plugin-match][service]")
{
    RendererHarness harness;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    service.submit (requestFor ("A"));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));
    service.cancel();
    REQUIRE (service.waitUntilFinished (3000));
    CHECK (service.poll().status == MatchRenderService::Status::cancelled);
    // The fake renderer waits on its own gate; without releasing it B queues behind A and the
    // next wait can time out. Cancel does not interrupt that wait.
    harness.releaseA();

    harness.throwOnB = true;
    service.submit (requestFor ("B"));
    REQUIRE (service.waitUntilFinished (3000));
    const auto failed = service.poll();
    CHECK (failed.status == MatchRenderService::Status::failed);
    CHECK (failed.error.contains ("boom"));
}

TEST_CASE ("A blocked plan reaches the UI with its reason", "[plugin-match][service]")
{
    RendererHarness harness;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    MatchPlan plan;
    plan.action = MatchAction::blocked;
    plan.reason = MatchBlockReason::modeMismatch;
    service.submitBlocked ("bad", plan, matchBlockReasonText (plan.reason));
    const auto state = service.poll();
    CHECK (state.status == MatchRenderService::Status::blocked);
    CHECK (state.key == "bad");
    CHECK (state.error == "Mode mismatch; cannot match scale");
}

TEST_CASE ("Host tempo changes are debounced to the last stable request",
           "[plugin-match][service]")
{
    RendererHarness harness;
    harness.openA = true;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    MatchRequestScheduler scheduler (service, 250);
    scheduler.request (requestFor ("A"), 0);
    scheduler.tick (100);
    CHECK (service.poll().status == MatchRenderService::Status::idle);
    scheduler.request (requestFor ("B"), 200);
    scheduler.tick (300);
    CHECK (service.poll().status == MatchRenderService::Status::idle);
    scheduler.tick (450);
    REQUIRE (service.waitUntilFinished (3000));
    CHECK (service.poll().key == "B");
    const auto seen = harness.keys();
    CHECK (seen.size() == 1);
    CHECK (seen.front() == "B");
}

TEST_CASE ("Re-planning a steady tempo does not restart the debounce window",
           "[plugin-match][service]")
{
    RendererHarness harness;
    harness.openA = true;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    MatchRequestScheduler scheduler (service, 250);
    scheduler.request (requestFor ("A"), 0);
    scheduler.tick (100);
    scheduler.request (requestFor ("A"), 200); // the timer saw the same key again
    scheduler.tick (260);
    REQUIRE (service.waitUntilFinished (3000));
    CHECK (service.poll().key == "A");
    CHECK (harness.keys().size() == 1);
}

TEST_CASE ("A scheduler-blocked request keeps its key and reason", "[plugin-match][service]")
{
    RendererHarness harness;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    MatchRequestScheduler scheduler (service, 250);
    MatchPlan plan;
    plan.action = MatchAction::blocked;
    plan.reason = MatchBlockReason::modeMismatch;
    scheduler.requestBlocked ("bad-key", plan, "Mode mismatch; cannot match scale");
    const auto state = service.poll();
    CHECK (state.status == MatchRenderService::Status::blocked);
    CHECK (state.key == "bad-key");
    CHECK (state.error == "Mode mismatch; cannot match scale");
}

TEST_CASE ("A request without a result key fails instead of staying Preparing",
           "[plugin-match][service]")
{
    RendererHarness harness;
    MatchRenderService service ([&] (const MatchRenderRequest& r, CancelCheck c)
                               { return harness (r, std::move (c)); });
    service.submit (requestFor (""));
    REQUIRE (service.waitUntilFinished (3000));
    const auto state = service.poll();
    CHECK (state.status == MatchRenderService::Status::failed);
    CHECK (state.error.contains ("no result key"));
}
