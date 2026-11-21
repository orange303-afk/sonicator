#include "ContributionUploader.h"

namespace sonicator::contribute {

Uploader::Uploader(std::unique_ptr<Transport> transportToUse, Source sourceToUse,
                   juce::String appVersionToUse, juce::String osNameToUse)
    : transport(std::move(transportToUse)), source(std::move(sourceToUse)),
      appVersion(std::move(appVersionToUse)), osName(std::move(osNameToUse))
{
}

juce::String Uploader::makeUuid()
{
    return juce::Uuid().toDashedString().toLowerCase();
}

juce::String Uploader::previewJson()
{
    const auto installId = source.installId ? source.installId() : juce::String();
    const auto records = source.fetch ? source.fetch(source.watermark ? source.watermark() : 0,
                                                     maxRecordsPerBatch)
                                      : std::vector<Record>();
    const auto vocabulary = source.vocabulary ? source.vocabulary() : Vocabulary();
    return buildBatch(records, vocabulary,
                      installId.isEmpty() ? juce::String("00000000-0000-4000-8000-000000000000") : installId,
                      makeUuid(), appVersion, osName, juce::Time::getCurrentTime()).json;
}

Uploader::Outcome Uploader::uploadOnce()
{
    Outcome outcome;
    if (transport == nullptr || ! source.allowed || ! source.allowed())
        return outcome;

    const auto installId = source.installId ? source.installId() : juce::String();
    const auto endpoint = source.endpoint ? source.endpoint() : juce::String();
    if (installId.isEmpty() || ! endpoint.startsWith("https://"))
    {
        outcome.error = "contribution is not configured";
        return outcome;
    }

    const auto watermark = source.watermark ? source.watermark() : 0;
    const auto records = source.fetch ? source.fetch(watermark, maxRecordsPerBatch) : std::vector<Record>();
    if (records.empty()) return outcome;

    const auto batch = buildBatch(records, source.vocabulary ? source.vocabulary() : Vocabulary(),
                                  installId, makeUuid(), appVersion, osName, juce::Time::getCurrentTime());
    if (batch.recordCount == 0) return outcome;

    const auto response = transport->post(endpoint + "/v1/contribute", batch.json);
    outcome.status = response.status;
    outcome.recordCount = batch.recordCount;
    outcome.error = response.error;

    if (response.status == 410)
    {
        // The install id was deleted from the panel or on another machine: start over with a new
        // one and resend from the beginning under it.
        if (source.storeInstallId) source.storeInstallId(makeUuid());
        if (source.commitWatermark) source.commitWatermark(0);
        outcome.replacedId = true;
        return outcome;
    }
    if (response.status < 200 || response.status >= 300)
        return outcome;

    if (source.commitWatermark) source.commitWatermark(batch.watermark);
    outcome.sent = true;
    outcome.error.clear();
    return outcome;
}

Uploader::Outcome Uploader::deleteMine()
{
    Outcome outcome;
    if (transport == nullptr) return outcome;
    const auto installId = source.installId ? source.installId() : juce::String();
    const auto endpoint = source.endpoint ? source.endpoint() : juce::String();
    if (installId.isEmpty() || ! endpoint.startsWith("https://"))
    {
        outcome.error = "contribution is not configured";
        return outcome;
    }

    const auto response = transport->remove(endpoint + "/v1/contributions/" + installId);
    outcome.status = response.status;
    outcome.error = response.error;
    if (response.status < 200 || response.status >= 300)
        return outcome;

    // A new id makes the deletion final: the old prefix is gone and nothing can be sent under it.
    if (source.storeInstallId) source.storeInstallId(makeUuid());
    if (source.commitWatermark) source.commitWatermark(0);
    outcome.sent = true;
    outcome.replacedId = true;
    return outcome;
}

Scheduler::Scheduler(Uploader& uploaderToRun, std::function<bool()> enabledCheck)
    : juce::Thread("Sonicator contribution"), uploader(uploaderToRun), enabled(std::move(enabledCheck))
{
}

Scheduler::~Scheduler()
{
    stop();
}

int64_t schedule::delayAfter (const Uploader::Outcome& outcome, bool attempted, int consecutiveFailures)
{
    if (! attempted || outcome.sent) return cycleMs;
    // Nothing was queued, or the contribution is not configured: there is no point retrying sooner.
    if (outcome.recordCount == 0 && outcome.status == 0) return cycleMs;

    int64_t delay = retryMs;
    const int failures = juce::jlimit(1, 8, consecutiveFailures);
    for (int i = 1; i < failures; ++i)
        delay = juce::jmin(delay * 2, retryCapMs);
    return delay;
}

void Scheduler::start()
{
    if (! isThreadRunning()) startThread();
}

void Scheduler::stop()
{
    wakeEvent.signal();
    stopThread(20000);
}

void Scheduler::requestNow()
{
    wakeEvent.signal();
}

void Scheduler::run()
{
    int64_t dueAt = juce::Time::currentTimeMillis() + schedule::firstDelayMs;
    int failures = 0;
    while (! threadShouldExit())
    {
        const auto now = juce::Time::currentTimeMillis();
        if (now >= dueAt)
        {
            const bool attempted = enabled != nullptr && enabled();
            Uploader::Outcome outcome;
            if (attempted) outcome = uploader.uploadOnce();
            const bool failed = attempted && ! outcome.sent && outcome.recordCount > 0;
            failures = failed ? failures + 1 : 0;
            if (onOutcome != nullptr) onOutcome(outcome, attempted);
            dueAt = juce::Time::currentTimeMillis()
                  + schedule::delayAfter(outcome, attempted, juce::jmax(1, failures));
            continue;
        }

        // Wake early on stop() and requestNow(); a short cap keeps the clock honest across sleep.
        const auto waitMs = (int) juce::jmin<int64_t>(dueAt - now, 60 * 1000);
        if (wakeEvent.wait((int) juce::jmax(1, waitMs)))
        {
            wakeEvent.reset();
            if (threadShouldExit()) return;
            dueAt = 0;
        }
    }
}

} // namespace sonicator::contribute
