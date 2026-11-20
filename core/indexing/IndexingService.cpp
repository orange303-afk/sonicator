#include "IndexingService.h"
#include "core/disc/DiscContentIndexer.h"
#include "core/smart/AssetAnalyzer.h"
#include <deque>
#include <memory>
#include <thread>
#include <unordered_set>
namespace sonicator {
namespace {
struct AnalysisOutcome
{
    AudioFileInfo entry;
    SampleRecord result;
};
// One analysis thread. Decoding, DSP and hashing are independent per file; the writer below is
// the only thread that touches the catalog.
class AnalysisWorker final : private juce::Thread
{
public:
    explicit AnalysisWorker(std::function<void()> work)
        : Thread("Library analysis"), body(std::move(work)) {}
    bool start() { return startThread(Priority::low); } // below normal, so audio and UI stay responsive
    void join() { waitForThreadToExit(-1); }
private:
    void run() override { body(); }
    std::function<void()> body;
};
}
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
    return startFolders(roots, force);
}
bool IndexingService::startFolders(const juce::StringArray& roots, bool force)
{
    return beginScan(roots, force, ScanMode::Full, false);
}
bool IndexingService::startFoldersNewOnly(const juce::StringArray& roots)
{
    return beginScan(roots, false, ScanMode::NewOnly, false);
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
    // The flags are atomics, but each waiter evaluates its predicate under one of these mutexes.
    // Passing through both after the store closes the check-then-wait window, so a Cancel that
    // lands at the wrong moment cannot be missed.
    { std::lock_guard lock(pauseMutex); }
    { std::lock_guard lock(pipelineMutex); }
    pauseCondition.notify_all();
    pipelineChanged.notify_all();
    pipelineSpace.notify_all();
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
        if (row.containerPath.isNotEmpty()) continue; // synthetic path, not a file on disk
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
void IndexingService::analyzePending(const std::vector<AudioFileInfo>& pending)
{
    const int workerCount = juce::jlimit(1, 4,
        (int) juce::jmax(1u, std::thread::hardware_concurrency()));
    { std::lock_guard lock(stateMutex); state.workers = workerCount; }
    std::deque<AnalysisOutcome> completed;
    std::atomic<size_t> nextIndex { 0 };
    // Bumped before a worker starts, so a fast first file cannot decrement before the increment.
    std::atomic<int> runningWorkers { 0 };
    // Set when the writer cannot continue (a catalog write threw). Unlike Cancel it drops every
    // unqueued result but must still release the workers: they are joined before the rethrow.
    std::atomic<bool> abort { false };
    bool allWorkersDone = false; // guarded by pipelineMutex
    // Bounded, so a fast analyzer cannot pile thousands of records up in memory.
    const size_t maxPending = (size_t) workerCount * 2 + 1;
    const auto stopRequested = [this, &abort] { return cancelled.load() || abort.load(); };

    struct WorkerDone
    {
        std::atomic<int>& running;
        bool& allDone;
        std::mutex& mutex;
        std::condition_variable& changed;
        // A scope guard: a throwing worker body still books its completion instead of leaving
        // the writer waiting for a thread that no longer exists.
        ~WorkerDone()
        {
            {
                std::lock_guard lock(mutex);
                if (--running == 0) allDone = true;
            }
            changed.notify_all();
        }
    };

    auto body = [this, &pending, &completed, &nextIndex, &runningWorkers, &allWorkersDone,
                 maxPending, &stopRequested]
    {
        WorkerDone done { runningWorkers, allWorkersDone, pipelineMutex, pipelineChanged };
        for (;;)
        {
            if (stopRequested()) break;
            const size_t index = nextIndex.fetch_add(1);
            if (index >= pending.size()) break;
            if (checkpoint()) break; // pause and cancel are honoured between files
            const auto& entry = pending[index];
            { std::lock_guard lock(stateMutex); state.current = entry.file.getFileName(); }
            SampleRecord result;
            try
            {
                result = FileScanner::isMetadataOnlyFormat(entry.file)
                    ? analyzeMetadataAsset(entry.file, [this] { return checkpoint(); })
                    : BasicAnalyzer().analyzeAndHash(entry.file, [this] { return checkpoint(); });
            }
            catch (const std::exception& e)
            {
                result.filePath = entry.file.getFullPathName();
                result.fileName = entry.file.getFileName();
                result.isLink = entry.file.isSymbolicLink();
                result.error = juce::String::fromUTF8(e.what());
                result.version = FileScanner::isMetadataOnlyFormat(entry.file)
                    ? metadataOnlyVersion : basicFeaturesVersion;
            }
            catch (...)
            {
                result.filePath = entry.file.getFullPathName();
                result.fileName = entry.file.getFileName();
                result.isLink = entry.file.isSymbolicLink();
                result.error = "Analysis failed";
                result.version = FileScanner::isMetadataOnlyFormat(entry.file)
                    ? metadataOnlyVersion : basicFeaturesVersion;
            }
            // Cancelled work keeps the previous row: the writer never sees this record.
            if (stopRequested()) break;
            {
                std::unique_lock lock(pipelineMutex);
                pipelineSpace.wait(lock, [&stopRequested, &completed, maxPending]
                    { return stopRequested() || completed.size() < maxPending; });
                if (stopRequested()) break;
                completed.push_back({ entry, std::move(result) });
            }
            pipelineChanged.notify_one();
        }
    };

    std::vector<std::unique_ptr<AnalysisWorker>> workers;
    for (int i = 0; i < workerCount; ++i)
    {
        ++runningWorkers;
        auto worker = std::make_unique<AnalysisWorker>(body);
        if (worker->start())
        {
            workers.push_back(std::move(worker));
        }
        else
        {
            // A thread the OS refused to start must not leave the writer waiting forever.
            std::lock_guard lock(pipelineMutex);
            if (--runningWorkers == 0) allWorkersDone = true;
            pipelineChanged.notify_one();
        }
    }

    // The service thread is the only writer, so SQLite sees one connection and one order.
    try
    {
        for (;;)
        {
            AnalysisOutcome outcome;
            {
                std::unique_lock lock(pipelineMutex);
                pipelineChanged.wait(lock, [&]
                    { return ! completed.empty() || allWorkersDone || stopRequested(); });
                if (completed.empty())
                {
                    if (allWorkersDone || stopRequested()) break;
                    continue;
                }
                outcome = std::move(completed.front());
                completed.pop_front();
            }
            pipelineSpace.notify_one();
            if (stopRequested()) break;
            repo.upsert(outcome.result);
            const bool duplicate = outcome.result.fileHash.isNotEmpty()
                && repo.countHash(outcome.result.fileHash) > 1;
            std::lock_guard lock(stateMutex);
            ++state.processed;
            if (outcome.result.error.isEmpty()) ++state.analyzed; else ++state.failed;
            if (duplicate) ++state.duplicates;
        }
    }
    catch (...)
    {
        // Abort the pipeline and join every worker before the exception reaches run(), so no
        // thread is left parked in the queue while the service unwinds.
        {
            std::lock_guard lock(pipelineMutex);
            abort = true;
        }
        pipelineChanged.notify_all();
        pipelineSpace.notify_all();
        for (auto& worker : workers) worker->join();
        throw;
    }

    for (auto& worker : workers) worker->join();
}
void IndexingService::run()
{
    try
    {
        std::vector<AudioFileInfo> files;
        std::vector<juce::File> discImages;
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
                {
                    if (isDiscContentCandidate(f.file)) discImages.push_back(f.file);
                    files.push_back(std::move(f));
                }
        }
        { std::lock_guard lock(stateMutex); state.total = (int) files.size();
          state.message = scanMode == ScanMode::NewOnly ? "Analyzing new files" : "Analyzing"; }
        // Precheck on this thread: every skip rule and every catalog lookup stays sequential,
        // and only the files that really need decoding reach the workers.
        std::vector<AudioFileInfo> pending;
        pending.reserve(files.size());
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
            pending.push_back(entry);
        }
        if (! cancelled && ! pending.empty()) analyzePending(pending);
        if (! cancelled && scanMode != ScanMode::NewOnly && ! discImages.empty())
        {
            { std::lock_guard lock(stateMutex); state.message = "Indexing disc contents"; }
            const auto summary = indexDiscContents(repo, discImages, [this] { return checkpoint(); },
                [this](int current, int total, const juce::String& name)
                {
                    std::lock_guard lock(stateMutex);
                    state.message = "Indexing disc contents " + juce::String(current) + "/"
                        + juce::String(total) + ": " + name;
                });
            { std::lock_guard lock(stateMutex);
              state.discImages = summary.imagesIndexed;
              state.discSkipped = summary.imagesSkipped;
              state.discFailed = summary.imagesFailed;
              state.discEntries = summary.entries;
              if (summary.lastError.isNotEmpty()) state.discMessage = summary.lastError; }
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
            repo.pruneDiscContents();
        }
        if (! cancelled)
        {
            { std::lock_guard lock(stateMutex); state.message = "Updating duplicate groups"; }
            // refreshDuplicates checks this inside BEGIN IMMEDIATE while holding the database
            // mutex. A pause there would freeze every catalog write on the message thread until
            // the user resumes, so this short phase only honours Cancel.
            repo.refreshDuplicates([this] { return cancelled.load(); });
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
