#include "AutoTagService.h"
#include "core/smart/AcousticClassifierService.h"
#include "core/dsp/AnalysisAudio.h"
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
    for (const auto& tag : tags)
    {
        options.tagIds[tag.name] = tag.id;
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
            std::vector<AutoTagProposal> proposalsToProcess = decision.proposals;
            if (proposalsToProcess.empty() && decision.proposal.has_value())
                proposalsToProcess.push_back(*decision.proposal);

            if (! proposalsToProcess.empty())
            {
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
                    for (auto& proposal : proposalsToProcess)
                    {
                        proposal.fileSize = size;
                        proposal.fileModified = modified;
                        if (result.proposals.size() < maxProposals)
                            result.proposals.push_back(std::move(proposal));
                        else
                            result.truncated = true;
                    }
                }
            }
            else
            {
                const bool skipHasConflict = decision.skip.has_value()
                    && (decision.skip->code == "conflicting-types" || decision.skip->code == "override-conflict");

                bool adaptiveProposed = false;
                if (! skipHasConflict)
                {
                    auto& adaptive = AcousticClassifierService::getSharedAdaptiveClassifier();
                    if (adaptive.hasPrototypes())
                    {
                        std::vector<float> features;
                        auto allFb = repo.allFeedback();
                        for (const auto& fb : allFb)
                        {
                            if (fb.sampleId == id) { features = fb.features; break; }
                        }
                        if (features.empty())
                        {
                            const juce::File file(record->filePath);
                            if (file.existsAsFile())
                            {
                                AnalysisAudioOptions opts;
                                opts.maxSeconds = 30.0;
                                const auto audio = readAnalysisAudio(file, opts);
                                if (audio.buffer.getNumSamples() > 0 && audio.sampleRate > 0.0)
                                    features = AdaptiveClassifier::extractFeatures(audio.buffer, audio.sampleRate);
                            }
                        }
                        if (! features.empty())
                        {
                            auto tagPreds = adaptive.predictTags(features, 1);
                            if (! tagPreds.empty() && tagPreds.front().score >= 0.85f)
                            {
                                const auto tagLabel = tagPreds.front().label;
                                const auto lower = tagLabel.toLowerCase();
                                bool plausible = true;
                                if (features.size() >= 13)
                                {
                                    const float duration = features[12]; // duration / 30.0
                                    if ((lower == "pad" || lower == "pads" || lower == "ambient") && duration < 0.025f)
                                        plausible = false;
                                    if ((lower == "loop" || lower == "loops") && duration < 0.04f)
                                        plausible = false;
                                }
                                if (plausible)
                                {
                                    int64_t targetTagId = 0;
                                    for (const auto& pair : options.tagIds)
                                    {
                                        if (pair.first.equalsIgnoreCase(tagLabel))
                                        {
                                            targetTagId = pair.second;
                                            break;
                                        }
                                    }
                                    if (targetTagId > 0)
                                    {
                                        const juce::File file(record->filePath);
                                        AutoTagProposal prop;
                                        prop.sampleId = id;
                                        prop.filePath = record->filePath;
                                        prop.fileName = record->fileName;
                                        prop.canonicalType = tagLabel;
                                        prop.tagId = targetTagId;
                                        prop.rule = AutoTagRule::SmartType;
                                        prop.source = "user_adaptive";
                                        prop.reason = "learned from acoustically similar prototypes";
                                        prop.fileSize = file.getSize();
                                        prop.fileModified = fileModified(file);
                                        for (const auto& assigned : repo.tagsForSample(id))
                                        {
                                            if (assigned.id == targetTagId) { prop.alreadyAssigned = true; break; }
                                        }
                                        if (prop.alreadyAssigned) ++result.alreadyAssigned;
                                        else ++result.newAssignments;
                                        result.proposals.push_back(std::move(prop));
                                        adaptiveProposed = true;
                                    }
                                }
                            }
                        }
                    }
                }
                if (! adaptiveProposed && decision.skip.has_value())
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
                if (tag.id == proposal.tagId || (proposal.tagId <= 0 && tag.name.equalsIgnoreCase(proposal.canonicalType)))
                { proposal.alreadyAssigned = true; break; }
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

    constexpr uint32_t autoTagColours[] = {
        0xffe05a5a, 0xffe08a45, 0xffe0b84a, 0xffd7dd6a, 0xff8fd06a,
        0xff4fb8c8, 0xff4f8fd0, 0xff7a6aea, 0xffa96ae0, 0xffd06ab8
    };

    for (auto& proposal : toApply)
    {
        if (proposal.tagId <= 0 && proposal.canonicalType.isNotEmpty())
        {
            int64_t tagId = 0;
            for (const auto& t : repo.tags())
                if (t.name.equalsIgnoreCase(proposal.canonicalType)) { tagId = t.id; break; }
            if (tagId == 0)
            {
                const auto colour = autoTagColours[repo.tags().size() % std::size(autoTagColours)];
                try { tagId = repo.createTag(proposal.canonicalType, colour); }
                catch (...) {}
            }
            if (tagId > 0)
            {
                proposal.tagId = tagId;
                liveTags.insert(tagId);
            }
        }
    }

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
    if (result.applied > 0)
    {
        std::vector<int64_t> appliedIds;
        for (const auto& p : toApply)
            if (p.sampleId > 0) appliedIds.push_back(p.sampleId);
        std::sort(appliedIds.begin(), appliedIds.end());
        appliedIds.erase(std::unique(appliedIds.begin(), appliedIds.end()), appliedIds.end());

        for (auto id : appliedIds)
        {
            if (cancelled) break;
            const auto record = repo.findById(id);
            if (! record) continue;
            const juce::File f(record->filePath);
            if (! f.existsAsFile()) continue;

            std::vector<float> features;
            for (const auto& fb : repo.allFeedback())
            {
                if (fb.sampleId == id) { features = fb.features; break; }
            }
            if (features.empty())
            {
                AnalysisAudioOptions opts;
                opts.maxSeconds = 30.0;
                const auto audio = readAnalysisAudio(f, opts);
                if (audio.buffer.getNumSamples() > 0 && audio.sampleRate > 0.0)
                    features = AdaptiveClassifier::extractFeatures(audio.buffer, audio.sampleRate);
            }
            if (features.empty()) continue;

            const auto sampleTags = repo.tagsForSample(id);
            juce::StringArray tagNames;
            for (const auto& t : sampleTags) tagNames.add(t.name);

            juce::String type;
            const auto currentOverrides = repo.smartOverrides(id);
            const auto it = currentOverrides.find(SmartField::Type);
            if (it != currentOverrides.end() && ! it->second.cleared && ! it->second.notApplicable)
                type = it->second.textValue;
            else if (const auto res = repo.smartResult(id))
                type = res->type;

            // A row a person wrote is their confirmation; AutoTag must not overwrite it, nor the
            // prototype the classifier already learned from it.
            if (repo.hasUserFeedback(id)) continue;
            repo.recordFeedback(id, type, tagNames, features, SampleRepository::FeedbackOrigin::autoTag);
            AcousticClassifierService::getSharedAdaptiveClassifier().updatePrototype(id, type, tagNames, features);
        }
    }
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
