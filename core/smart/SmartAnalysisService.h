#pragma once
#include "core/database/SampleRepository.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <condition_variable>
namespace sonicator {
struct SmartProgress
{
    bool running = false, paused = false, cancelled = false;
    int processed = 0, failed = 0;
    juce::String current, message;
};
// Background metadata extraction. It never touches measured Phase 1 features and never runs on
// the audio thread. Cancelling keeps every completed sample; a paused job holds its position.
class SmartAnalysisService final : private juce::Thread
{
public:
    explicit SmartAnalysisService(SampleRepository& r) : Thread("Smart analysis"), repo(r) {}
    ~SmartAnalysisService() override;
    bool start();
    void cancel();
    void setPaused(bool);
    SmartProgress progress() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
    // Recomputes one sample synchronously (used when a sidecar may have changed while browsing).
    bool refreshSample(int64_t sampleId);
    static std::vector<SmartCandidate> collectCandidates(const SampleRecord&, juce::String* error = nullptr);
    static std::vector<SmartInput> currentInputs(const SampleRecord&);
    static bool inputsDiffer(const std::vector<SmartInput>& stored, const std::vector<SmartInput>& current);
private:
    SampleRepository& repo;
    mutable std::mutex stateMutex;
    SmartProgress state;
    std::atomic<bool> cancelled { false };
    std::mutex pauseMutex;
    std::condition_variable pauseCondition;
    bool paused = false;
    bool checkpoint();
    void run() override;
};
} // namespace sonicator
