#include "IndexingService.h"
#include "core/smart/AssetAnalyzer.h"
#include <unordered_set>
namespace sonicator {
IndexingService::~IndexingService() { cancel(); waitForThreadToExit(-1); }
bool IndexingService::start(const juce::StringArray& roots, bool force)
{
    return beginScan(roots, force, ScanMode::Full, true);
}
bool IndexingService::startNewOnly(const juce::StringArray& roots)
{
    return beginScan(roots, false, ScanMode::NewOnly, true);
}
bool IndexingService::startSubtree(const juce::String& folder, bool force)
{
    if (folder.isEmpty()) return false;
    juce::StringArray roots;
    roots.add(folder);
    return beginScan(roots, force, ScanMode::Full, false);
}
bool IndexingService::beginScan(const juce::StringArray& roots, bool force, ScanMode mode,
                                bool registerNewRoots)
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    folders = roots; forceAnalysis = force; scanMode = mode; registerRoots = registerNewRoots;
    cancelled = false;
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
std::vector<int64_t> findMissingIds(const std::vector<PathEntry>& rows,
                                    const std::unordered_set<std::string>& discoveredPaths,
                                    const juce::StringArray& roots,
                                    const std::vector<IgnoredPath>& ignored)
{
    std::vector<int64_t> missing;
    for (const auto& row : rows)
    {
        if (row.missing) continue;
        if (isPathIgnored(row.path, ignored)) continue;
        const juce::File file(row.path);
        bool inRoot = false;
        for (const auto& root : roots)
            if (file.isAChildOf(juce::File(root))) { inRoot = true; break; }
        if (! inRoot || discoveredPaths.find(row.path.toStdString()) != discoveredPaths.end()) continue;
        // Discovery can fail silently (offline volume, File Provider, I/O error), so an absent path is
        // only proof of deletion when the filesystem agrees. Candidates alone pay for the probe.
        if (! file.existsAsFile()) missing.push_back(row.id);
    }
    return missing;
}
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
        const auto ignored = repo.ignoredPaths();
        const auto shouldSkip = [ignored](const juce::File& f)
        { return isPathIgnored(f.getFullPathName(), ignored); };
        for (const auto& folder : folders)
        {
            if (checkpoint()) break;
            auto found = FileScanner::scanDirectory(juce::File(folder), [this] { return checkpoint(); },
                                                    shouldSkip);
            if (registerRoots) repo.addRoot(folder);
            for (auto& f : found)
                if (! shouldSkip(f.file)
                    && foundPaths.insert(f.file.getFullPathName().toStdString()).second)
                    files.push_back(std::move(f));
        }
        { std::lock_guard lock(stateMutex); state.total = (int) files.size();
          state.message = scanMode == ScanMode::NewOnly ? "Analyzing new files" : "Analyzing"; }
        for (const auto& entry : files)
        {
            if (checkpoint()) break;
            const auto& file = entry.file;
            { std::lock_guard lock(stateMutex); state.current = file.getFileName(); }
            auto previous = repo.findByPath(file.getFullPathName());
            if (scanMode == ScanMode::NewOnly && previous)
            {
                // Additive scan: a known path is never reanalyzed, even when changed or failed.
                std::lock_guard lock(stateMutex); ++state.processed; ++state.skipped; continue;
            }
            if (! forceAnalysis && previous && previous->error.isEmpty() && ! previous->missing
                && previous->fileSize == entry.fileSize && previous->dateModified == entry.dateModified
                && (previous->version == basicFeaturesVersion
                    || previous->version == metadataOnlyVersion))
            {
                std::lock_guard lock(stateMutex); ++state.processed; ++state.skipped; continue;
            }
            auto result = FileScanner::isMetadataOnlyFormat(file)
                ? analyzeMetadataAsset(file, [this] { return checkpoint(); })
                : BasicAnalyzer().analyzeAndHash(file, [this] { return checkpoint(); });
            if (cancelled) break;
            repo.upsert(result);
            bool duplicate = result.fileHash.isNotEmpty() && repo.countHash(result.fileHash) > 1;
            std::lock_guard lock(stateMutex);
            ++state.processed;
            if (result.error.isEmpty()) ++state.analyzed; else ++state.failed;
            if (duplicate) ++state.duplicates;
        }
        if (! cancelled && scanMode != ScanMode::NewOnly)
        {
            // Paths under a scanned root that discovery did not return are checked against the disk
            // before they are marked missing. No sort, no offset paging.
            { std::lock_guard lock(stateMutex); state.finalizing = true; state.message = "Checking for missing files"; }
            int64_t lastId = 0;
            for (;;)
            {
                if (checkpoint()) break;
                auto rows = repo.pathsAfterId(lastId, 5000);
                if (rows.empty()) break;
                lastId = rows.back().id;
                const auto missingIds = findMissingIds(rows, foundPaths, folders, ignored);
                if (! missingIds.empty()) repo.setMissing(missingIds);
            }
            { std::lock_guard lock(stateMutex); state.finalizing = false; }
        }
        if (! cancelled)
        {
            { std::lock_guard lock(stateMutex); state.message = "Updating duplicate groups"; }
            repo.refreshDuplicates();
        }
        std::lock_guard lock(stateMutex);
        state.message = cancelled ? "Cancelled; completed results were saved"
            : (scanMode == ScanMode::NewOnly ? "New-file scan complete" : "Scan complete");
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(stateMutex); state.message = juce::String::fromUTF8(e.what()); state.finalizing = false;
    }
    std::lock_guard lock(stateMutex);
    state.running = false; state.paused = false; state.cancelled = cancelled;
}
}
