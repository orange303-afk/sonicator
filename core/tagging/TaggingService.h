#pragma once
#include "core/database/SampleRepository.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <mutex>
#include <vector>
namespace sonicator {
enum class TaggingStage { Idle, Preparing, Applying, Complete, Cancelled, Failed };
// Progress for a bulk tag operation. The counts are real work, never a fabricated percentage.
struct TaggingProgress
{
    bool running = false, cancelled = false, clearing = false;
    TaggingStage stage = TaggingStage::Idle;
    int total = 0, processed = 0, applied = 0, errors = 0;
    juce::String operation, current, message, lastError;
};
// Applies one tag to a frozen set of catalog ids in bounded transactions. Folder scope is
// resolved once at start, so a file discovered later is not silently included. Cancellation
// takes effect between rows: the unfinished batch rolls back and completed batches stay.
class TaggingService final : private juce::Thread
{
public:
    explicit TaggingService(SampleRepository& r, int transactionBatch = 500)
        : Thread("Bulk tagging"), repo(r), batchSize(juce::jmax(1, transactionBatch)) {}
    ~TaggingService() override;
    bool startTagFolder(const juce::String& folder, int64_t tagId);
    bool startTagSelected(const std::vector<int64_t>& sampleIds, int64_t tagId);
    // Removes every tag link from the scope. Tag definitions and recent-tag history are untouched.
    bool startResetFolder(const juce::String& folder);
    bool startResetSelected(const std::vector<int64_t>& sampleIds);
    void cancel();
    TaggingProgress progress() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
    // Called on the worker after each committed batch. The UI may publish progress here; tests
    // use it to request cancellation at a deterministic boundary.
    std::function<void(const TaggingProgress&)> onBatch;
    // Called once on the worker after the job with every id whose tags may have changed, so the
    // owner can refresh learned prototypes and the contribution row.
    std::function<void(const std::vector<int64_t>&)> onTagsChanged;
private:
    SampleRepository& repo;
    const int batchSize;
    enum class Scope { Folder, Selected };
    Scope scope = Scope::Selected;
    juce::String folderScope;
    std::vector<int64_t> selectedScope;
    int64_t targetTag = 0;
    bool clearing = false;
    mutable std::mutex stateMutex;
    TaggingProgress state;
    std::atomic<bool> cancelled { false };
    bool begin(const juce::String& operation);
    void run() override;
};
} // namespace sonicator
