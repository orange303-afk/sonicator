#pragma once
#include "core/database/SampleRepository.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <set>
namespace sonicator {
enum class SmartStage { Idle, Scanning, Verifying, Extracting, Publishing, Complete, Cancelled, Failed };

struct SmartProgress
{
    bool running = false, paused = false, cancelled = false;
    SmartStage stage = SmartStage::Idle;
    int total = 0, processed = 0, failed = 0, stale = 0, verified = 0;
    juce::String current, message, lastError;
};

// Result of one read-only metadata pass. Every extractor records that it ran, including when it
// found nothing, so a later version bump re-runs it instead of keeping an empty result forever.
struct SmartCollection
{
    std::vector<SmartCandidate> candidates;
    std::vector<SmartExtraction> extractions;
    juce::String error;
};

// Background metadata extraction. It never touches measured Phase 1 features and never runs on
// the audio thread. Cancelling keeps every completed sample and the last completed generation.
class SmartAnalysisService final : private juce::Thread
{
public:
    explicit SmartAnalysisService(SampleRepository& r) : Thread("Smart analysis"), repo(r) {}
    ~SmartAnalysisService() override;
    // Incremental pass: missing/stale/old-version results first, then fingerprint verification
    // of completed results so a changed, added or deleted sidecar is discovered at run start.
    bool start();
    // Bounded queue of explicit samples, e.g. after an override or from the inspector.
    bool startSelected(const std::vector<int64_t>& sampleIds);
    // Queues one sample without blocking the message thread. Starts a selected job when idle.
    bool requestSample(int64_t sampleId);
    void cancel();
    void setPaused(bool);
    SmartProgress progress() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
    // Synchronous; worker and tests only. UI code must use requestSample() so file I/O and
    // parsing never run on the message thread.
    bool refreshSample(int64_t sampleId);
    static SmartCollection collect(const SampleRecord&);
    static std::vector<SmartCandidate> collectCandidates(const SampleRecord&, juce::String* error = nullptr);
    static std::vector<SmartInput> currentInputs(const SampleRecord&);
    static bool inputsDiffer(const std::vector<SmartInput>& stored, const std::vector<SmartInput>& current);
private:
    enum class Mode { Incremental, Selected };
    SampleRepository& repo;
    mutable std::mutex stateMutex;
    SmartProgress state;
    std::atomic<bool> cancelled { false };
    std::mutex pauseMutex;
    std::condition_variable pauseCondition;
    bool paused = false;
    std::mutex queueMutex;
    std::deque<int64_t> queue;
    std::set<int64_t> queued;
    Mode mode = Mode::Incremental;
    bool checkpoint();
    bool takeQueued(int64_t& sampleId);
    void run() override;
    bool processSample(int64_t sampleId, const juce::String& catalogPath);
    void verifySample(int64_t sampleId);
    void setStage(SmartStage stage, const juce::String& message);
};
} // namespace sonicator
