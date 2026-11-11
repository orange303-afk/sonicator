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

// Runs Uploader on a background thread: once after a short delay, then once a day. Every failure
// is silent to the user by design; the next cycle simply tries again.
class Scheduler final : private juce::Thread
{
public:
    Scheduler(Uploader& uploader, std::function<bool()> enabled);
    ~Scheduler() override;
    void start();
    void stop();
private:
    void run() override;
    Uploader& uploader;
    std::function<bool()> enabled;
    juce::WaitableEvent stopEvent;
};

} // namespace sonicator::contribute
