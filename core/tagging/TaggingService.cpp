#include "TaggingService.h"
namespace sonicator {
TaggingService::~TaggingService()
{
    cancel();
    waitForThreadToExit(-1);
}
bool TaggingService::startTagFolder(const juce::String& folder, int64_t tagId)
{
    if (folder.isEmpty() || tagId <= 0) return false;
    clearing = false;
    if (! begin("Tag folder")) return false;
    scope = Scope::Folder;
    folderScope = folder;
    selectedScope.clear();
    targetTag = tagId;
    if (startThread()) return true;
    std::lock_guard lock(stateMutex);
    state.running = false; state.stage = TaggingStage::Failed;
    state.message = "Cannot start tagging thread";
    return false;
}
bool TaggingService::startTagSelected(const std::vector<int64_t>& sampleIds, int64_t tagId)
{
    if (sampleIds.empty() || tagId <= 0) return false;
    clearing = false;
    if (! begin("Tag selected files")) return false;
    scope = Scope::Selected;
    folderScope.clear();
    selectedScope = sampleIds;
    targetTag = tagId;
    if (startThread()) return true;
    std::lock_guard lock(stateMutex);
    state.running = false; state.stage = TaggingStage::Failed;
    state.message = "Cannot start tagging thread";
    return false;
}
bool TaggingService::begin(const juce::String& operation)
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    cancelled = false;
    std::lock_guard lock(stateMutex);
    state = {};
    state.running = true;
    state.clearing = clearing;
    state.stage = TaggingStage::Preparing;
    state.operation = operation;
    state.message = "Preparing";
    return true;
}
bool TaggingService::startResetFolder(const juce::String& folder)
{
    if (folder.isEmpty()) return false;
    clearing = true;
    if (! begin("Reset tags in folder")) return false;
    scope = Scope::Folder;
    folderScope = folder;
    selectedScope.clear();
    targetTag = 0;
    if (startThread()) return true;
    std::lock_guard lock(stateMutex);
    state.running = false; state.stage = TaggingStage::Failed;
    state.message = "Cannot start tag reset";
    return false;
}
bool TaggingService::startResetSelected(const std::vector<int64_t>& sampleIds)
{
    if (sampleIds.empty()) return false;
    clearing = true;
    if (! begin("Reset tags in selection")) return false;
    scope = Scope::Selected;
    folderScope.clear();
    selectedScope = sampleIds;
    targetTag = 0;
    if (startThread()) return true;
    std::lock_guard lock(stateMutex);
    state.running = false; state.stage = TaggingStage::Failed;
    state.message = "Cannot start tag reset";
    return false;
}
void TaggingService::cancel()
{
    cancelled = true;
}
TaggingProgress TaggingService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}
void TaggingService::run()
{
    try
    {
        std::vector<int64_t> ids;
        if (scope == Scope::Folder)
        {
            {
                std::lock_guard lock(stateMutex);
                state.message = "Finding catalogued files";
            }
            ids = repo.idsInFolder(folderScope);
        }
        else
        {
            ids = selectedScope;
        }
        {
            std::lock_guard lock(stateMutex);
            state.total = (int) ids.size();
            state.stage = TaggingStage::Applying;
            state.message = clearing ? "Removing tags" : "Assigning tag";
            state.current = scope == Scope::Folder ? folderScope : juce::String("selected files");
        }
        for (size_t start = 0; start < ids.size();)
        {
            if (cancelled)
            {
                std::lock_guard lock(stateMutex);
                state.cancelled = true;
                break;
            }
            const size_t end = std::min(ids.size(), start + (size_t) batchSize);
            std::vector<int64_t> batch(ids.begin() + (long) start, ids.begin() + (long) end);
            SampleRepository::TagAssignmentOutcome outcome;
            const auto runBatch = [this](const std::vector<int64_t>& ids)
            {
                return clearing
                    ? repo.clearTagsBatch(ids, [this] { return cancelled.load(); })
                    : repo.assignTagBatch(ids, targetTag, [this] { return cancelled.load(); });
            };
            try
            {
                outcome = runBatch(batch);
            }
            catch (const std::exception& batchError)
            {
                // A database error must not discard the work of earlier batches. Retry this batch
                // one file at a time, count the failures and keep going.
                for (auto id : batch)
                {
                    if (cancelled) break;
                    try
                    {
                        auto single = runBatch({ id });
                        if (single.cancelled) { outcome.cancelled = true; break; }
                        outcome.added += single.added;
                    }
                    catch (const std::exception& fileError)
                    {
                        std::lock_guard lock(stateMutex);
                        ++state.errors;
                        state.lastError = juce::String::fromUTF8(fileError.what());
                    }
                }
                if (outcome.added == 0 && ! outcome.cancelled)
                {
                    std::lock_guard lock(stateMutex);
                    if (state.lastError.isEmpty()) state.lastError = juce::String::fromUTF8(batchError.what());
                }
            }
            if (outcome.cancelled)
            {
                std::lock_guard lock(stateMutex);
                state.cancelled = true;
                break;
            }
            TaggingProgress snapshot;
            {
                std::lock_guard lock(stateMutex);
                state.processed += (int) batch.size();
                state.applied += outcome.added;
                snapshot = state;
            }
            if (onBatch) onBatch(snapshot);
            start = end;
        }
        {
            std::lock_guard lock(stateMutex);
            const int applied = state.applied;
            if (! clearing && applied > 0)
            {
                try { repo.recordRecentTag(targetTag); }
                catch (...) {}
            }
            state.running = false;
            state.current.clear();
            if (state.cancelled)
            {
                state.stage = TaggingStage::Cancelled;
                state.message = clearing
                    ? "Cancelled; " + juce::String(applied) + " assignment(s) removed"
                    : "Cancelled; " + juce::String(applied) + " assignment(s) kept";
            }
            else
            {
                state.stage = TaggingStage::Complete;
                state.message = clearing
                    ? "Removed " + juce::String(applied) + " tag assignment(s)"
                    : "Tagged " + juce::String(applied) + " file(s)";
            }
        }
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.stage = TaggingStage::Failed;
        state.lastError = juce::String::fromUTF8(e.what());
        state.message = "Tagging failed: " + state.lastError;
    }
}
} // namespace sonicator
