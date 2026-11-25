#pragma once
#include "FileDisposal.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <mutex>
#include <vector>

namespace sonicator::files
{
enum class DuplicateAction { Trash, DeletePermanently, ReplaceWithLink };

struct DuplicateCleanupJob
{
    // The real file that stays. Every redundant copy must be byte-identical to it.
    juce::File original;
    juce::File copy;
    DuplicateAction action = DuplicateAction::Trash;
    juce::String hash; // catalog group hash, used to refresh only the affected groups
};

enum class DuplicateCleanupState { removed, linked, skipped, failed, cancelled };

struct DuplicateCleanupResult
{
    DuplicateCleanupJob job;
    DuplicateCleanupState state = DuplicateCleanupState::skipped;
    juce::String message;
    // Size of the copy measured before the action; 0 for anything that was not changed.
    int64_t bytes = 0;
};

struct DuplicateCleanupProgress
{
    bool running = false, cancelled = false;
    int total = 0, processed = 0, removed = 0, linked = 0, skipped = 0, failed = 0;
    // Bytes whose content is gone for good (permanent deletion, or the old copy after a verified
    // link). Trashed bytes still occupy the disk until Trash is emptied, so they are reported apart.
    int64_t freedBytes = 0, trashedBytes = 0;
    juce::String current;
};

// Same size and identical bytes. A link is compared through its target.
bool filesAreIdentical(const juce::File& a, const juce::File& b, juce::String& error);

// True when both paths name the same file on disk, whatever the spelling: a symbolic link and its
// target, an 8.3 short path and the long one, or a different letter case. A link whose target is
// missing is not the same file.
bool sameFile(const juce::File& a, const juce::File& b);

// One background job at a time. Each copy is verified against its original immediately before
// the action, never from catalog metadata alone.
class DuplicateCleanupService final : private juce::Thread
{
public:
    DuplicateCleanupService();
    ~DuplicateCleanupService() override;
    bool start(const std::vector<DuplicateCleanupJob>&, juce::File journalFile = {});
    void cancel();
    DuplicateCleanupProgress progress() const;
    std::vector<DuplicateCleanupResult> results() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
    // Restores the real file of every link replacement an interrupted run left half done.
    // Returns one line per entry it handled.
    static juce::StringArray recoverInterrupted(const juce::File& journalFile);
    static juce::File defaultJournalFile();
private:
    std::vector<DuplicateCleanupJob> jobs;
    juce::File journal;
    std::atomic<bool> cancelled { false };
    mutable std::mutex stateMutex;
    DuplicateCleanupProgress state;
    std::vector<DuplicateCleanupResult> fileResults;
    void run() override;
};
} // namespace sonicator::files
