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
        std::unordered_set<std::string> seen;
        for (const auto& folder : folders)
        {
            if (checkpoint()) break;
            auto found = FileScanner::scanDirectory(juce::File(folder), [this] { return checkpoint(); });
            repo.addRoot(folder);
            for (auto& f : found)
                if (seen.insert(f.file.getFullPathName().toStdString()).second) files.push_back(std::move(f));
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
            auto result = BasicAnalyzer().analyze(file, [this] { return checkpoint(); });
            if (cancelled) break;
            if (result.error.isEmpty())
            {
                result.fileHash = FileScanner::computeFileHash(file, [this] { return checkpoint(); });
                if (file.getSize() != entry.fileSize || file.getLastModificationTime().toMilliseconds() != entry.dateModified)
                {
                    result.error = "File changed during analysis; rescan to retry"; result.version = 0;
                    result.fileHash.clear();
                }
            }
            repo.upsert(result);
            bool duplicate = result.fileHash.isNotEmpty() && repo.countHash(result.fileHash) > 1;
            std::lock_guard lock(stateMutex);
            ++state.processed;
            if (result.error.isEmpty()) ++state.analyzed; else ++state.failed;
            if (duplicate) ++state.duplicates;
        }
        if (! cancelled)
        {
            // Missing paths remain in the catalog. Only reconcile roots that were successfully traversed.
            for (int offset = 0;; offset += 1000)
            {
                auto records = repo.page(1000,offset);
                if (records.empty()) break;
                for (auto& r : records)
                {
                    if (checkpoint()) break;
                    juce::File file(r.filePath);
                    bool inRoot = false;
                    for (auto& root : folders) if (file.isAChildOf(juce::File(root))) { inRoot = true; break; }
                    if (inRoot && ! file.existsAsFile() && ! r.missing) { r.missing = true; repo.upsert(r); }
                }
                if (cancelled) break;
            }
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
