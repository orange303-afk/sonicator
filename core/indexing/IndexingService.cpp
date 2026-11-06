#include "IndexingService.h"
#include <unordered_set>
namespace sonicator {
IndexingService::~IndexingService() { cancel(); waitForThreadToExit(-1); }
bool IndexingService::start(const juce::StringArray& roots, bool force)
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    folders = roots; forceAnalysis = force; cancelled = false;
    { std::lock_guard lock(pauseMutex); paused = false; }
    { std::lock_guard lock(stateMutex); state = {}; state.running = true; state.message = "Discovering audio files..."; }
    if (! startThread())
    {
        std::lock_guard lock(stateMutex); state.running = false; state.message = "Cannot start indexing thread"; return false;
    }
    return true;
}
void IndexingService::cancel()
{
    cancelled = true;
    pauseCondition.notify_all();
}
void IndexingService::setPaused(bool value)
{
    { std::lock_guard lock(pauseMutex); paused = value; }
    { std::lock_guard lock(stateMutex); state.paused = value; }
    pauseCondition.notify_all();
}
IndexingProgress IndexingService::progress() const { std::lock_guard lock(stateMutex); return state; }
bool IndexingService::checkpoint()
{
    std::unique_lock lock(pauseMutex);
    pauseCondition.wait(lock, [this] { return ! paused || cancelled.load(); });
    return cancelled;
}
void IndexingService::run()
{
    try
    {
        std::vector<AudioFileInfo> files;
        std::unordered_set<std::string> foundPaths;
        for (const auto& folder : folders)
        {
            if (checkpoint()) break;
            auto found = FileScanner::scanDirectory(juce::File(folder), [this] { return checkpoint(); });
            repo.addRoot(folder);
            for (auto& f : found)
                if (foundPaths.insert(f.file.getFullPathName().toStdString()).second) files.push_back(std::move(f));
        }
        { std::lock_guard lock(stateMutex); state.total = (int) files.size(); state.message = "Analyzing"; }
        for (const auto& entry : files)
        {
            if (checkpoint()) break;
            const auto& file = entry.file;
            { std::lock_guard lock(stateMutex); state.current = file.getFileName(); }
            auto previous = repo.findByPath(file.getFullPathName());
            if (! forceAnalysis && previous && previous->error.isEmpty() && ! previous->missing
                && previous->fileSize == entry.fileSize && previous->dateModified == entry.dateModified
                && previous->version == analysisVersion)
            {
                std::lock_guard lock(stateMutex); ++state.processed; ++state.skipped; continue;
            }
            auto result = BasicAnalyzer().analyzeAndHash(file, [this] { return checkpoint(); });
            if (cancelled) break;
            repo.upsert(result);
            bool duplicate = result.fileHash.isNotEmpty() && repo.countHash(result.fileHash) > 1;
            std::lock_guard lock(stateMutex);
            ++state.processed;
            if (result.error.isEmpty()) ++state.analyzed; else ++state.failed;
            if (duplicate) ++state.duplicates;
        }
        if (! cancelled)
        {
            // Paths that are under a scanned root but were not discovered are gone. Discovery already
            // enumerated every path, so this pass needs no count, no sort and no filesystem probe.
            { std::lock_guard lock(stateMutex); state.finalizing = true; state.message = "Checking for missing files"; }
            int64_t lastId = 0;
            for (;;)
            {
                if (checkpoint()) break;
                auto rows = repo.pathsAfterId(lastId, 5000);
                if (rows.empty()) break;
                std::vector<int64_t> missingIds;
                for (const auto& row : rows)
                {
                    lastId = row.id;
                    if (row.missing) continue;
                    const juce::File file(row.path);
                    bool inRoot = false;
                    for (const auto& root : folders)
                        if (file.isAChildOf(juce::File(root))) { inRoot = true; break; }
                    if (inRoot && foundPaths.find(row.path.toStdString()) == foundPaths.end())
                        missingIds.push_back(row.id);
                }
                if (! missingIds.empty()) repo.setMissing(missingIds);
            }
            { std::lock_guard lock(stateMutex); state.finalizing = false; }
        }
        std::lock_guard lock(stateMutex);
        state.message = cancelled ? "Cancelled; completed results were saved" : "Scan complete";
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(stateMutex); state.message = juce::String::fromUTF8(e.what());
    }
    std::lock_guard lock(stateMutex);
    state.running = false; state.paused = false; state.cancelled = cancelled;
}
}
