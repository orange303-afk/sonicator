#pragma once
#include "ContributionPayload.h"
#include <juce_events/juce_events.h>

namespace sonicator::contribute {

// One upload cycle: fetch records changed since the per-catalog watermark, build a batch, send it
// and only then move the watermark. No UI, no logging of content, no retries inside one cycle.
class Uploader
{
public:
    struct Source
    {
        // False when the toggle is off, consent is missing or stale, or the install id is unset.
        std::function<bool()> allowed;
        std::function<juce::String()> installId;
        std::function<void(const juce::String&)> storeInstallId;
        std::function<juce::String()> endpoint;
        std::function<int64_t()> watermark;
        std::function<void(int64_t)> commitWatermark;
        std::function<std::vector<Record>(int64_t, int)> fetch;
        std::function<Vocabulary()> vocabulary;
    };

    struct Outcome
    {
        bool sent = false;        // a 2xx arrived and the watermark moved
        bool replacedId = false;  // the intake answered 410 and a fresh install id was issued
        bool morePending = false; // a full batch went out and the watermark still has records
        int status = 0;
        int recordCount = 0;
        juce::String error;
    };

    Uploader(std::unique_ptr<Transport> transport, Source source,
             juce::String appVersion, juce::String osName);

    // Sends at most one batch. Returns sent == true only after the intake accepted it.
    Outcome uploadOnce();
    // Deletes everything stored under the current install id, then issues a new one.
    Outcome deleteMine();

    static juce::String makeUuid();
    // JSON of the next batch, for the "what will be sent" preview. Sends nothing.
    juce::String previewJson();

private:
    std::unique_ptr<Transport> transport;
    Source source;
    juce::String appVersion, osName;
};

// When the background cycle may try again. A rejected or unreachable batch is retried within the
// session instead of waiting a whole day, so a broken deploy or a transient outage costs minutes.
namespace schedule {
constexpr int64_t firstDelayMs = 60 * 1000;
constexpr int64_t cycleMs = 24 * 60 * 60 * 1000;
constexpr int64_t retryMs = 10 * 60 * 1000;
constexpr int64_t retryCapMs = 6 * 60 * 60 * 1000;
// A batch is bounded by the intake's body size, so a large backlog needs several requests. Three
// minutes keeps the drain under the intake's 30 requests an hour from one address.
constexpr int64_t drainMs = 3 * 60 * 1000;

// attempted == false means the toggle, consent or install id was missing: nothing was sent.
// consecutiveFailures is 1 for the first failure and grows while the intake keeps refusing.
int64_t delayAfter (const Uploader::Outcome& outcome, bool attempted, int consecutiveFailures);
}

// Runs Uploader on a background thread: once shortly after start, then once a day, and sooner after
// a failure. Every attempt is reported through onOutcome; the thread never touches the UI itself.
class Scheduler final : private juce::Thread
{
public:
    Scheduler(Uploader& uploader, std::function<bool()> enabled);
    ~Scheduler() override;
    void start();
    void stop();
    // Uploads as soon as the thread can, instead of waiting for the next scheduled attempt.
    // Safe from any thread: the settings toggle and a new confirmed label both use it.
    void requestNow();
    // Called on the scheduler thread after every attempt, including a refused one.
    std::function<void(const Uploader::Outcome&, bool attempted)> onOutcome;
private:
    void run() override;
    Uploader& uploader;
    std::function<bool()> enabled;
    juce::WaitableEvent wakeEvent;
};

} // namespace sonicator::contribute
