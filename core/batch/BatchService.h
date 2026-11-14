#pragma once
#include "BatchTypes.h"
#include <atomic>
#include <mutex>

namespace sonicator::batch
{
// One background job at a time. The owner polls progress() at UI rate and reads results() after
// the thread stopped; the worker never calls UI code. Every planned output appears in the report,
// including ones that were never started because of a cancellation.
class BatchService final : private juce::Thread
{
public:
    BatchService();
    ~BatchService() override;
    // A second start while a job runs is ignored. `reportFolder` is optional; when set, the
    // finished report is written there and old diagnostics are pruned (20 files / 30 days).
    bool start(const PreparedBatch&, juce::File reportFolder = {});
    void cancel();
    Progress progress() const;
    Report results() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
    bool isRunning() const { return running.load(); }
    // The report of the last finished job; also used by the report export in the dialog.
    static juce::String reportToJson(const Report&);
    static juce::String reportToText(const Report&);
    // Writes atomically and prunes this folder's own batch-*.json files. Returns the file.
    static juce::File saveReport(const Report&, const juce::File& folder, int maxFiles = 20,
                                 int maxAgeDays = 30);
private:
    void run() override;
    PreparedBatch batch;
    juce::File reportFolder;
    std::atomic<bool> running { false }, cancelled { false };
    mutable std::mutex stateMutex;
    Progress state;
    Report report;
};
} // namespace sonicator::batch
