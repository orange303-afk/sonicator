#include "AutoTagService.h"
#include <algorithm>
#include <set>
namespace sonicator {
namespace {
constexpr size_t maxScopeIds = 1'000'000;
constexpr size_t maxProposals = 50'000;
constexpr size_t maxSkippedRows = 10'000;
AutoTagOptions optionsFromRepository(SampleRepository& repo)
{
    AutoTagOptions options;
    const auto tags = repo.tags();
    for (const auto& type : AutoTagRules::supportedTypes())
    {
        for (const auto& tag : tags)
            if (tag.name.equalsIgnoreCase(type))
            {
                options.tagIds[type] = tag.id;
                break;
            }
    }
    return options;
}
int64_t fileModified(const juce::File& file)
{
    return file.getLastModificationTime().toMilliseconds();
}
}
AutoTagService::~AutoTagService()
{
    cancel();
    waitForThreadToExit(-1);
}
bool AutoTagService::begin(Job nextJob, const juce::String& message)
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    cancelled = false;
    job = nextJob;
    {
        std::lock_guard lock(stateMutex);
        state = {};
        state.running = true;
        state.stage = nextJob == Job::Prepare ? AutoTagStage::Preparing : AutoTagStage::Applying;
        state.message = message;
    }
    return true;
}
bool AutoTagService::startSelected(const std::vector<int64_t>& sampleIds)
{
    if (sampleIds.empty()) return false;
    if (! begin(Job::Prepare, "Preparing AutoTag proposals")) return false;
    folderScope = false;
    folder.clear();
    selectedIds = sampleIds;
    if (startThread()) return true;
    std::lock_guard lock(stateMutex);
    state.running = false; state.stage = AutoTagStage::Failed;
    state.message = "Cannot start AutoTag";
    return false;
}
bool AutoTagService::startFolder(const juce::String& path)
{
    if (path.isEmpty()) return false;
    if (! begin(Job::Prepare, "Preparing AutoTag proposals")) return false;
    folderScope = true;
    folder = path;
    selectedIds.clear();
    if (startThread()) return true;
    std::lock_guard lock(stateMutex);
    state.running = false; state.stage = AutoTagStage::Failed;
    state.message = "Cannot start AutoTag";
    return false;
}
bool AutoTagService::startApply(std::vector<AutoTagProposal> proposals)
{
    if (proposals.empty()) return false;
    if (! begin(Job::Apply, "Applying AutoTag proposals")) return false;
    toApply = std::move(proposals);
    {
        std::lock_guard lock(stateMutex);
        state.total = (int) toApply.size();
    }
    if (startThread()) return true;
    std::lock_guard lock(stateMutex);
    state.running = false; state.stage = AutoTagStage::Failed;
    state.message = "Cannot start AutoTag application";
    return false;
}
void AutoTagService::cancel()
{
    cancelled = true;
}
AutoTagProgress AutoTagService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}
bool AutoTagService::takeResult(AutoTagResult& out)
{
    std::lock_guard lock(resultMutex);
    if (! hasPrepared) return false;
    out = std::move(prepared);
    prepared = {};
    hasPrepared = false;
    return true;
}
AutoTagApplyResult AutoTagService::applyResult() const
{
    std::lock_guard lock(resultMutex);
    return applied;
}
void AutoTagService::run()
{
    try
    {
        if (job == Job::Prepare) prepare();
        else apply();
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.stage = AutoTagStage::Failed;
        state.lastError = juce::String::fromUTF8(e.what());
        state.message = "AutoTag failed: " + state.lastError;
    }
}
void AutoTagService::prepare()
{
    std::vector<int64_t> ids;
    if (folderScope)
    {
        ids = repo.idsInFolder(folder);
    }
    else
    {
        ids = repo.visibleIds(selectedIds);
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    {
        std::lock_guard lock(stateMutex);
        state.total = (int) ids.size();
        state.current.clear();
    }
    if (ids.size() > maxScopeIds)
    {
        AutoTagResult result;
        result.message = "Too many catalogued files in this scope; narrow the folder.";
        {
            std::lock_guard lock(resultMutex);
            prepared = std::move(result);
            hasPrepared = true;
        }
        std::lock_guard lock(stateMutex);
        state.running = false; state.stage = AutoTagStage::Failed;
        state.message = "Too many catalogued files in this scope; narrow the folder.";
        return;
    }
    const auto options = optionsFromRepository(repo);
    AutoTagResult result;
    result.scope = folderScope ? folder : juce::String("selected files");
    for (auto id : ids)
    {
        if (cancelled)
        {
            std::lock_guard lock(stateMutex);
            state.cancelled = true;
            break;
        }
        try
        {
            const auto record = repo.findById(id);
            if (! record)
            {
                std::lock_guard lock(stateMutex);
                ++state.processed;
                continue;
            }
            {
                std::lock_guard lock(stateMutex);
                state.current = record->fileName;
            }
            AutoTagInput input;
            input.sampleId = id;
            input.filePath = record->filePath;
            input.candidates = repo.smartCandidates(id);
            input.result = repo.smartResult(id);
            const auto overrides = repo.smartOverrides(id);
            if (const auto found = overrides.find(SmartField::Type); found != overrides.end())
                input.typeOverride = found->second;
            const auto decision = AutoTagRules::evaluate(input, options);
            if (decision.proposal.has_value())
            {
                auto proposal = *decision.proposal;
                const juce::File file(record->filePath);
                const auto size = file.getSize();
                const auto modified = fileModified(file);
                const bool recordHasFingerprint = record->fileSize > 0 && record->dateModified > 0;
                if (! file.existsAsFile()
                    || (recordHasFingerprint
                        && (size != record->fileSize || modified != record->dateModified)))
                {
                    AutoTagSkip changed;
                    changed.sampleId = id;
                    changed.filePath = record->filePath;
                    changed.fileName = record->fileName;
                    changed.code = "changed-file";
                    changed.reason = "the file changed or is missing since it was indexed";
                    if (result.skipped.size() < maxSkippedRows) result.skipped.push_back(std::move(changed));
                    ++result.skippedCount;
                    std::lock_guard lock(stateMutex);
                    ++state.skipped;
                }
                else
                {
                    proposal.fileSize = size;
                    proposal.fileModified = modified;
                    if (result.proposals.size() >= maxProposals)
                    {
                        result.truncated = true;
                        break;
                    }
                    result.proposals.push_back(std::move(proposal));
                }
            }
            else if (decision.skip.has_value())
            {
                if (decision.skip->code == "no-evidence")
                {
                    ++result.noEvidence;
                }
                else
                {
                    if (result.skipped.size() < maxSkippedRows) result.skipped.push_back(*decision.skip);
                    ++result.skippedCount;
                    std::lock_guard lock(stateMutex);
                    ++state.skipped;
                }
            }
            ++result.checked;
        }
        catch (const std::exception& e)
        {
            std::lock_guard lock(stateMutex);
            ++state.errors;
            state.lastError = juce::String::fromUTF8(e.what());
            ++result.errors;
        }
        AutoTagProgress snapshot;
        {
            std::lock_guard lock(stateMutex);
            ++state.processed;
            state.proposals = (int) result.proposals.size();
            snapshot = state;
        }
        if (onSample) onSample(snapshot);
        if (result.truncated) break;
    }
    if (! result.proposals.empty())
    {
        std::vector<int64_t> proposalIds;
        proposalIds.reserve(result.proposals.size());
        for (const auto& proposal : result.proposals) proposalIds.push_back(proposal.sampleId);
        const auto assigned = repo.tagsForSamples(proposalIds);
        for (auto& proposal : result.proposals)
        {
            const auto found = assigned.find(proposal.sampleId);
            if (found == assigned.end()) continue;
            for (const auto& tag : found->second)
                if (tag.id == proposal.tagId) { proposal.alreadyAssigned = true; break; }
            proposal.included = ! proposal.alreadyAssigned;
            if (proposal.alreadyAssigned) ++result.alreadyAssigned;
        }
        result.newAssignments = (int) result.proposals.size() - result.alreadyAssigned;
    }
    result.message = result.truncated
        ? "Stopped at the proposal limit; narrow the folder."
        : "Prepared " + juce::String(result.proposals.size()) + " proposal(s) for "
            + juce::String(result.checked) + " checked file(s)";
    {
        std::lock_guard lock(resultMutex);
        prepared = std::move(result);
        hasPrepared = true;
    }
    std::lock_guard lock(stateMutex);
    state.running = false;
    state.current.clear();
    if (state.cancelled)
    {
        state.stage = AutoTagStage::Cancelled;
        state.message = "AutoTag preparation cancelled; no tags were written";
    }
    else
    {
        state.stage = AutoTagStage::Complete;
        state.message = "AutoTag preparation complete";
    }
}
void AutoTagService::apply()
{
    std::set<int64_t> liveTags;
    for (const auto& tag : repo.tags()) liveTags.insert(tag.id);
    AutoTagApplyResult result;
    for (size_t start = 0; start < toApply.size(); start += (size_t) batchSize)
    {
        if (cancelled)
        {
            result.cancelled = true;
            break;
        }
        const size_t end = std::min(toApply.size(), start + (size_t) batchSize);
        std::vector<SampleRepository::TagPair> pairs;
        int rejected = 0;
        for (size_t i = start; i < end; ++i)
        {
            const auto& proposal = toApply[i];
            {
                std::lock_guard lock(stateMutex);
                state.current = proposal.fileName;
            }
            bool valid = proposal.sampleId > 0 && proposal.tagId > 0
                && liveTags.count(proposal.tagId) != 0
                && repo.findById(proposal.sampleId).has_value();
            if (valid)
            {
                const juce::File file(proposal.filePath);
                valid = file.existsAsFile()
                    && file.getSize() == proposal.fileSize
                    && fileModified(file) == proposal.fileModified;
            }
            if (! valid) { ++rejected; continue; }
            pairs.push_back({ proposal.sampleId, proposal.tagId });
        }
        SampleRepository::TagAssignmentOutcome outcome;
        try
        {
            outcome = repo.assignTagPairs(pairs, [this] { return cancelled.load(); });
        }
        catch (const std::exception& batchError)
        {
            for (const auto& pair : pairs)
            {
                if (cancelled) break;
                try
                {
                    const auto single = repo.assignTagPairs({ pair },
                        [this] { return cancelled.load(); });
                    if (single.cancelled) { outcome.cancelled = true; break; }
                    outcome.added += single.added;
                }
                catch (const std::exception& fileError)
                {
                    ++result.errors;
                    std::lock_guard lock(stateMutex);
                    state.lastError = juce::String::fromUTF8(fileError.what());
                }
            }
            if (result.errors == 0 && ! outcome.cancelled)
            {
                std::lock_guard lock(stateMutex);
                if (state.lastError.isEmpty()) state.lastError = juce::String::fromUTF8(batchError.what());
            }
        }
        if (outcome.cancelled)
        {
            result.cancelled = true;
            break;
        }
        result.applied += outcome.added;
        result.rejected += rejected;
        AutoTagProgress snapshot;
        {
            std::lock_guard lock(stateMutex);
            state.processed += (int) (end - start);
            state.applied = result.applied;
            state.errors = result.rejected + result.errors;
            snapshot = state;
        }
        if (onApplyBatch) onApplyBatch(snapshot);
    }
    result.errors += result.rejected;
    result.message = result.cancelled
        ? "Cancelled; " + juce::String(result.applied) + " assignment(s) kept"
        : "Applied " + juce::String(result.applied) + " assignment(s)";
    {
        std::lock_guard lock(resultMutex);
        applied = result;
    }
    std::lock_guard lock(stateMutex);
    state.running = false;
    state.stage = result.cancelled ? AutoTagStage::Cancelled : AutoTagStage::Complete;
    state.message = result.message;
    state.cancelled = result.cancelled;
}
} // namespace sonicator
