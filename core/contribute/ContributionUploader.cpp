#include "ContributionUploader.h"

namespace sonicator::contribute {
namespace {
constexpr int firstDelayMs = 60 * 1000;
constexpr int cycleMs = 24 * 60 * 60 * 1000;
}

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

void Scheduler::start()
{
    if (! isThreadRunning()) startThread();
}

void Scheduler::stop()
{
    stopEvent.signal();
    stopThread(20000);
}

void Scheduler::run()
{
    if (stopEvent.wait(firstDelayMs)) return;
    while (! threadShouldExit())
    {
        if (enabled && enabled()) uploader.uploadOnce();
        if (stopEvent.wait(cycleMs)) return;
    }
}

} // namespace sonicator::contribute
