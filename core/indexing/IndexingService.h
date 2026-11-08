#pragma once
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <condition_variable>
#include <unordered_set>
#include <vector>
namespace sonicator {
struct IndexingProgress
{
    bool running = false, paused = false, cancelled = false, finalizing = false;
    int total = 0, processed = 0, analyzed = 0, skipped = 0, failed = 0, duplicates = 0;
    juce::String current, message;
};
// Rows under the scanned roots that are not in the discovery set and not on disk. The filesystem
// probe runs only for candidates, so an unchanged scan stays allocation-and-index only.
std::vector<int64_t> findMissingIds(const std::vector<PathEntry>& rows,
                                    const std::unordered_set<std::string>& discoveredPaths,
                                    const juce::StringArray& roots,
                                    const std::vector<IgnoredPath>& ignored = {});
enum class ScanMode { Full, NewOnly };
class IndexingService final : private juce::Thread
{
public:
    explicit IndexingService(SampleRepository& r) : Thread("Library indexing"), repo(r) {}
    ~IndexingService();
    bool start(const juce::StringArray& roots, bool force = false);
    // Discovers the registered folders but analyzes only paths that are not in the catalog yet.
    // Known files are left untouched, including changed, failed and older-version records.
    bool startNewOnly(const juce::StringArray& roots);
    // Scans one folder and its descendants without registering it as a new imported root. Missing
    // reconciliation is restricted to that subtree, so an unavailable folder elsewhere is not
    // mistaken for deleted files.
    bool startSubtree(const juce::String& folder, bool force = false);
    void cancel();
    void setPaused(bool);
    IndexingProgress progress() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
private:
    SampleRepository& repo;
    juce::StringArray folders;
    bool forceAnalysis = false;
    bool registerRoots = true;
    ScanMode scanMode = ScanMode::Full;
    mutable std::mutex stateMutex;
    IndexingProgress state;
    std::atomic<bool> cancelled { false };
    std::mutex pauseMutex;
    std::condition_variable pauseCondition;
    bool paused = false;
    bool beginScan(const juce::StringArray& roots, bool force, ScanMode mode, bool registerRoots);
    bool checkpoint();
    void run() override;
};
}
