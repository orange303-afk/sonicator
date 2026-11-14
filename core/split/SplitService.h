#pragma once
#include "SilenceDetector.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <vector>
namespace sonicator {
struct SplitOptions
{
    SilenceOptions silence;
    juce::File outputFolder;
    bool sameFolder = true;
    bool moveToTrash = false;
    // Each written part is peak-normalized to -0.1 dBFS on its own, with one gain for all its
    // channels (the batch design's "steps after Split act on every fragment").
    bool normalize = false;
};
enum class SplitFileState { split, noSplit, skipped, failed, cancelled };
struct SplitFileResult
{
    juce::File source;
    std::vector<juce::File> parts;
    juce::String error;
    SplitFileState state = SplitFileState::skipped;
    int detectedParts = 0;
    int discardedShortParts = 0;
    // True only when every detected part was written and validated, nothing was dropped, and
    // the batch was not cancelled. Only this state may offer the original for deletion.
    bool deletable() const
    {
        return state == SplitFileState::split && discardedShortParts == 0;
    }
};
// A cancelled batch never offers deletion, even for files that finished before cancellation.
inline bool deletionAllowed(const SplitFileResult& result, bool batchCancelled)
{
    return ! batchCancelled && result.deletable();
}
// Existing sidecar files that belong to a source: Ableton's <file>.wav.asd and the common
// <stem>.asd / <stem>.pk variants. Missing candidates are not returned.
juce::StringArray splitSidecarPaths(const juce::File& source);
struct SplitProgress
{
    bool running = false, cancelled = false;
    int total = 0, processed = 0, succeeded = 0, failed = 0, skipped = 0;
    juce::String current, message;
};
// One background job at a time. Sources are opened read-only; the service never deletes or moves
// them. The owner polls progress() and reads results() after the thread stops.
class SplitService final : private juce::Thread
{
public:
    SplitService() : Thread("Silence split") {}
    ~SplitService() override;
    bool start(const std::vector<juce::File>& sources, const SplitOptions& options);
    void cancel();
    SplitProgress progress() const;
    std::vector<SplitFileResult> results() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
private:
    std::vector<juce::File> files;
    SplitOptions splitOptions;
    std::atomic<bool> cancelled { false };
    mutable std::mutex stateMutex;
    SplitProgress state;
    std::vector<SplitFileResult> fileResults;
    void run() override;
};
} // namespace sonicator
