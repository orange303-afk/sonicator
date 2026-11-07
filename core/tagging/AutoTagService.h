#pragma once
#include "AutoTagRules.h"
#include "core/database/SampleRepository.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <mutex>
namespace sonicator {
enum class AutoTagStage { Idle, Preparing, Applying, Complete, Cancelled, Failed };
struct AutoTagProgress
{
    bool running = false, cancelled = false;
    AutoTagStage stage = AutoTagStage::Idle;
    int total = 0, processed = 0, proposals = 0, skipped = 0, errors = 0, applied = 0;
    juce::String current, message, lastError;
};
struct AutoTagResult
{
    std::vector<AutoTagProposal> proposals;
    std::vector<AutoTagSkip> skipped; // notable reasons; no-evidence is counted only
    int checked = 0, noEvidence = 0, skippedCount = 0, alreadyAssigned = 0, newAssignments = 0, errors = 0;
    bool truncated = false;
    juce::String scope, message;
};
struct AutoTagApplyResult
{
    int applied = 0, rejected = 0, errors = 0;
    bool cancelled = false;
    juce::String message;
};
// Prepares AutoTag proposals off the UI thread and writes nothing. Application revalidates the
// frozen scope, the tag and the file before each batch. One job at a time.
class AutoTagService final : private juce::Thread
{
public:
    explicit AutoTagService(SampleRepository& r, int applyBatch = 500)
        : Thread("AutoTag"), repo(r), batchSize(juce::jmax(1, applyBatch)) {}
    ~AutoTagService() override;
    bool startSelected(const std::vector<int64_t>& sampleIds);
    bool startFolder(const juce::String& folder);
    bool startApply(std::vector<AutoTagProposal> proposals);
    void cancel();
    AutoTagProgress progress() const;
    bool takeResult(AutoTagResult&);
    AutoTagApplyResult applyResult() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
    // Called on the worker after each checked sample, so progress can be shown and a test can
    // stop preparation deterministically.
    std::function<void(const AutoTagProgress&)> onSample;
    // Called on the worker after each applied batch.
    std::function<void(const AutoTagProgress&)> onApplyBatch;
private:
    enum class Job { Prepare, Apply };
    SampleRepository& repo;
    const int batchSize;
    Job job = Job::Prepare;
    bool folderScope = false;
    juce::String folder;
    std::vector<int64_t> selectedIds;
    std::vector<AutoTagProposal> toApply;
    mutable std::mutex stateMutex;
    AutoTagProgress state;
    mutable std::mutex resultMutex;
    AutoTagResult prepared;
    bool hasPrepared = false;
    AutoTagApplyResult applied;
    std::atomic<bool> cancelled { false };
    bool begin(Job, const juce::String& message);
    void prepare();
    void apply();
    void run() override;
};
} // namespace sonicator
