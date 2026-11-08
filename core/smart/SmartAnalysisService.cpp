#include "SmartAnalysisService.h"
#include "AbletonAsd.h"
#include "AiffMetadata.h"
#include "NameHeuristics.h"
#include "Rex2Metadata.h"
#include "SmartResolver.h"
#include "WavMetadata.h"
#include <algorithm>
namespace sonicator {
namespace {
constexpr size_t selectedQueueLimit = 10000;
constexpr int batchSize = 64;
bool isAppleDouble(const juce::File& file) { return file.getFileName().startsWith("._"); }
void append(std::vector<SmartCandidate>& destination, std::vector<SmartCandidate> source)
{
    destination.insert(destination.end(), std::make_move_iterator(source.begin()),
                       std::make_move_iterator(source.end()));
}
juce::String fingerprintOf(const SmartInput& input)
{
    return juce::String(input.size) + ":" + juce::String(input.modified);
}
juce::String extractionSourceForCandidate(const juce::String& source, const juce::String& container)
{
    if (source == "filename" || source == "folder") return "name";
    if (source.startsWith("wav_")) return "wav";
    if (source.startsWith("aiff_")) return "aiff";
    if (source.startsWith("rx2_")) return "rx2";
    if (source.startsWith("asd_")) return "asd";
    if (source == "info" || source == "id3") return container;
    if (source == "dsp") return "dsp";
    return "other";
}
// Every candidate carries the file it came from and that file's fingerprint at read time, so the
// inspector can explain provenance and the job can tell whether the value is still current.
void tagProvenance(std::vector<SmartCandidate>& candidates, const SampleRecord& record,
                   const std::vector<SmartInput>& inputs, const juce::String& container)
{
    const SmartInput* audio = nullptr;
    const SmartInput* sidecar = nullptr;
    for (const auto& input : inputs)
    {
        if (input.kind == "audio") audio = &input;
        else if (input.kind == "asd") sidecar = &input;
    }
    for (auto& c : candidates)
    {
        const auto source = extractionSourceForCandidate(c.source, container);
        const SmartInput* input = source == "asd" && sidecar != nullptr ? sidecar : audio;
        if (input == nullptr) { c.inputKind = "audio"; c.inputPath = record.filePath; }
        else { c.inputKind = input->kind; c.inputPath = input->path; c.fingerprint = fingerprintOf(*input); }
        c.extractorId = source;
        c.extractorVersion = source == "dsp" ? smartDspVersion : smartExtractorsVersion;
        if (c.extractedAt == 0) c.extractedAt = juce::Time::currentTimeMillis();
        if (c.rawValue.isEmpty()) c.rawValue = c.detail.isNotEmpty() ? c.detail : c.textValue;
        if (c.limitation.isEmpty())
        {
            if (c.source == "rx2_glob")
                c.limitation = "REX2 preview tempo, not necessarily the original tempo";
            else if (c.source == "asd_warp")
                c.limitation = "warp-marker tempo is not necessarily the original tempo";
            else if (c.source == "wav_smpl" || c.source == "wav_inst")
                c.limitation = "sampler root, never a musical key";
        }
    }
}
} // namespace

SmartAnalysisService::~SmartAnalysisService()
{
    cancel();
    waitForThreadToExit(-1);
}
void SmartAnalysisService::setStage(SmartStage stage, const juce::String& message)
{
    std::lock_guard lock(stateMutex);
    state.stage = stage;
    state.message = message;
}
bool SmartAnalysisService::start()
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    cancelled = false;
    { std::lock_guard lock(pauseMutex); paused = false; }
    {
        std::lock_guard lock(queueMutex);
        queue.clear(); queued.clear();
        mode = Mode::Incremental;
    }
    {
        std::lock_guard lock(stateMutex);
        state = {};
        state.running = true;
        state.stage = SmartStage::Scanning;
        state.message = "Finding missing and stale metadata";
    }
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.stage = SmartStage::Failed;
        state.message = "Cannot start smart analysis thread";
        return false;
    }
    return true;
}
bool SmartAnalysisService::startSelected(const std::vector<int64_t>& sampleIds)
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    cancelled = false;
    { std::lock_guard lock(pauseMutex); paused = false; }
    const auto visible = repo.visibleIds(sampleIds);
    {
        std::lock_guard lock(queueMutex);
        queue.clear(); queued.clear();
        for (auto id : visible)
            if (id > 0 && queued.insert(id).second) queue.push_back(id);
        if (queue.empty()) return false;
        mode = Mode::Selected;
    }
    {
        std::lock_guard lock(stateMutex);
        state = {};
        state.running = true;
        state.stage = SmartStage::Extracting;
        state.total = (int) visible.size();
        state.message = "Analyzing selected samples";
    }
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.stage = SmartStage::Failed;
        state.message = "Cannot start smart analysis thread";
        return false;
    }
    return true;
}
bool SmartAnalysisService::startFolder(const juce::String& folder)
{
    if (folder.isEmpty() || isThreadRunning()) return false;
    waitForThreadToExit(-1);
    cancelled = false;
    { std::lock_guard lock(pauseMutex); paused = false; }
    {
        std::lock_guard lock(queueMutex);
        queue.clear(); queued.clear();
        mode = Mode::Folder;
        folderScope = folder;
    }
    {
        std::lock_guard lock(stateMutex);
        state = {};
        state.running = true;
        state.stage = SmartStage::Extracting;
        state.total = repo.countInFolder(folder);
        state.message = "Analyzing folder metadata";
    }
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.stage = SmartStage::Failed;
        state.message = "Cannot start smart analysis thread";
        return false;
    }
    return true;
}
bool SmartAnalysisService::requestSample(int64_t sampleId)
{
    if (sampleId <= 0) return false;
    if (repo.visibleIds({ sampleId }).empty()) return false;
    {
        std::lock_guard lock(queueMutex);
        if (queued.count(sampleId) != 0) return true;
        if (queued.size() >= selectedQueueLimit) return false;
        queued.insert(sampleId);
        queue.push_back(sampleId);
    }
    if (isThreadRunning()) return true;
    // No job is running: start a selected job over the queue we just filled. start() cannot be
    // reused here because it clears the queue and switches to incremental mode.
    cancelled = false;
    { std::lock_guard lock(pauseMutex); paused = false; }
    int queuedCount = 0;
    {
        std::lock_guard lock(queueMutex);
        mode = Mode::Selected;
        queuedCount = (int) queue.size();
    }
    {
        std::lock_guard lock(stateMutex);
        state = {};
        state.running = true;
        state.stage = SmartStage::Extracting;
        state.total = queuedCount;
        state.message = "Analyzing selected samples";
    }
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.stage = SmartStage::Failed;
        state.message = "Cannot start smart analysis thread";
        return false;
    }
    return true;
}
void SmartAnalysisService::cancel()
{
    cancelled = true;
    pauseCondition.notify_all();
}
void SmartAnalysisService::setPaused(bool value)
{
    { std::lock_guard lock(pauseMutex); paused = value; }
    { std::lock_guard lock(stateMutex); state.paused = value; }
    pauseCondition.notify_all();
}
SmartProgress SmartAnalysisService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}
bool SmartAnalysisService::checkpoint()
{
    std::unique_lock lock(pauseMutex);
    pauseCondition.wait(lock, [this] { return ! paused || cancelled.load(); });
    return cancelled;
}
bool SmartAnalysisService::takeQueued(int64_t& sampleId)
{
    std::lock_guard lock(queueMutex);
    if (queue.empty()) return false;
    sampleId = queue.front();
    queue.pop_front();
    queued.erase(sampleId);
    return true;
}
SmartCollection SmartAnalysisService::collect(const SampleRecord& record)
{
    SmartCollection out;
    auto runExtractor = [&](const juce::String& source, auto&& extractor)
    {
        SmartExtraction extraction;
        extraction.source = source;
        extraction.extractorVersion = smartExtractorsVersion;
        extraction.updatedAt = juce::Time::currentTimeMillis();
        try
        {
            auto result = extractor();
            append(out.candidates, std::move(result.candidates));
            if (result.error.isNotEmpty() && out.error.isEmpty()) out.error = result.error;
            extraction.status = result.error.isNotEmpty() ? "error" : "ok";
            extraction.error = result.error;
        }
        catch (const std::exception& e)
        {
            extraction.status = "error";
            extraction.error = juce::String::fromUTF8(e.what());
            if (out.error.isEmpty()) out.error = extraction.error;
        }
        catch (...)
        {
            extraction.status = "error";
            extraction.error = "unknown extractor failure";
            if (out.error.isEmpty()) out.error = extraction.error;
        }
        out.extractions.push_back(std::move(extraction));
    };
    runExtractor("name", [&]()
    {
        struct Result { std::vector<SmartCandidate> candidates; juce::String error; };
        return Result{ extractNameCandidates(record.filePath), {} };
    });
    const juce::File file(record.filePath);
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension == ".wav")
        runExtractor("wav", [&]() { return extractWavMetadata(file); });
    else if (extension == ".aif" || extension == ".aiff" || extension == ".aifc")
        runExtractor("aiff", [&]() { return extractAiffMetadata(file); });
    else if (extension == ".rx2")
        runExtractor("rx2", [&]() { return extractRex2Metadata(file); });
    else
    {
        SmartExtraction extraction;
        extraction.source = "container";
        extraction.extractorVersion = smartExtractorsVersion;
        extraction.status = "unsupported";
        extraction.updatedAt = juce::Time::currentTimeMillis();
        out.extractions.push_back(std::move(extraction));
    }
    const auto sidecar = file.getSiblingFile(file.getFileName() + ".asd");
    if (sidecar.existsAsFile() && ! isAppleDouble(sidecar))
        runExtractor("asd", [&]() { return extractAsdMetadata(sidecar); });
    else
    {
        SmartExtraction extraction;
        extraction.source = "asd";
        extraction.extractorVersion = smartExtractorsVersion;
        extraction.status = "absent";
        extraction.updatedAt = juce::Time::currentTimeMillis();
        out.extractions.push_back(std::move(extraction));
    }
    return out;
}
std::vector<SmartCandidate> SmartAnalysisService::collectCandidates(const SampleRecord& record,
                                                                    juce::String* error)
{
    auto collection = collect(record);
    if (error != nullptr) *error = collection.error;
    return collection.candidates;
}
std::vector<SmartInput> SmartAnalysisService::currentInputs(const SampleRecord& record)
{
    std::vector<SmartInput> inputs;
    const juce::File audio(record.filePath);
    if (audio.existsAsFile())
        inputs.push_back({ "audio", record.filePath, audio.getSize(),
                           audio.getLastModificationTime().toMilliseconds() });
    else
        inputs.push_back({ "audio", record.filePath, record.fileSize, record.dateModified });
    const auto sidecar = audio.getSiblingFile(audio.getFileName() + ".asd");
    if (sidecar.existsAsFile() && ! isAppleDouble(sidecar))
        inputs.push_back({ "asd", sidecar.getFullPathName(), sidecar.getSize(),
                           sidecar.getLastModificationTime().toMilliseconds() });
    std::sort(inputs.begin(), inputs.end(), [](const SmartInput& a, const SmartInput& b)
    {
        return a.kind != b.kind ? a.kind < b.kind : a.path < b.path;
    });
    return inputs;
}
bool SmartAnalysisService::inputsDiffer(const std::vector<SmartInput>& stored,
                                        const std::vector<SmartInput>& current)
{
    if (stored.size() != current.size()) return true;
    for (size_t i = 0; i < stored.size(); ++i)
        if (stored[i].kind != current[i].kind || stored[i].path != current[i].path
            || stored[i].size != current[i].size || stored[i].modified != current[i].modified)
            return true;
    return false;
}
bool SmartAnalysisService::processSample(int64_t sampleId, const juce::String& catalogPath)
{
    auto record = repo.findById(sampleId);
    if (! record) return false;
    {
        std::lock_guard lock(stateMutex);
        state.stage = SmartStage::Extracting;
        state.current = record->fileName;
    }
    const auto before = currentInputs(*record);
    const auto revision = repo.smartRevision(sampleId);
    SmartCollection collection;
    try
    {
        collection = collect(*record);
    }
    catch (const std::exception& e)
    {
        repo.markSmartError(sampleId, "extract-exception", juce::String::fromUTF8(e.what()));
        return false;
    }
    const auto after = currentInputs(*record);
    if (inputsDiffer(before, after))
    {
        // A source changed while it was being read. Publish nothing: the previous completed
        // generation stays, marked stale, and the next pass reads the new contents.
        repo.markSmartStale(sampleId);
        std::lock_guard lock(stateMutex);
        ++state.stale;
        return false;
    }
    const auto extension = juce::File(record->filePath).getFileExtension().toLowerCase();
    const auto container = extension == ".wav" ? juce::String("wav")
        : extension == ".rx2" ? juce::String("rx2")
        : (extension == ".aif" || extension == ".aiff" || extension == ".aifc") ? juce::String("aiff")
        : juce::String("container");
    tagProvenance(collection.candidates, *record, before, container);
    const bool anyExtractionWorked = std::any_of(collection.extractions.begin(), collection.extractions.end(),
        [](const SmartExtraction& e) { return e.status == "ok"; });
    if (! anyExtractionWorked && collection.error.isNotEmpty())
    {
        repo.markSmartError(sampleId, "extract-error", collection.error);
        return false;
    }
    SmartPublication publication;
    publication.sampleId = sampleId;
    publication.expectedRevision = revision;
    publication.candidates = std::move(collection.candidates);
    publication.inputs = after;
    publication.extractions = std::move(collection.extractions);
    publication.catalogPath = catalogPath;
    {
        std::lock_guard lock(stateMutex);
        state.stage = SmartStage::Publishing;
    }
    const auto outcome = repo.publishSmartAnalysis(publication);
    if (! outcome.published)
    {
        if (outcome.reason == "revision-changed")
        {
            // A newer result (or an override edit) won the race. Never overwrite it.
            std::lock_guard lock(stateMutex);
            ++state.stale;
            return false;
        }
        repo.markSmartError(sampleId, outcome.reason, "publication refused");
        return false;
    }
    return true;
}
void SmartAnalysisService::verifySample(int64_t sampleId)
{
    auto record = repo.findById(sampleId);
    if (! record) return;
    const auto stored = repo.smartInputs(sampleId);
    const auto current = currentInputs(*record);
    if (stored.empty() || inputsDiffer(stored, current))
    {
        repo.markSmartStale(sampleId);
        std::lock_guard lock(stateMutex);
        ++state.stale;
    }
}
bool SmartAnalysisService::refreshSample(int64_t sampleId)
{
    if (isThreadRunning()) return false;
    return processSample(sampleId, repo.catalogPath());
}
void SmartAnalysisService::run()
{
    const auto catalogPath = repo.catalogPath();
    const auto progressSample = [this]
    {
        std::lock_guard lock(stateMutex);
        ++state.processed;
    };
    try
    {
        // Explicit requests are processed first and again between incremental batches, so a
        // refresh from the inspector does not have to wait for a whole catalog pass.
        const auto processQueued = [&]()
        {
            int64_t queuedId = 0;
            while (! cancelled && takeQueued(queuedId))
            {
                if (checkpoint()) return;
                try
                {
                    const bool ok = processSample(queuedId, catalogPath);
                    progressSample();
                    if (! ok)
                    {
                        std::lock_guard lock(stateMutex);
                        ++state.failed;
                    }
                }
                catch (const std::exception& e)
                {
                    repo.markSmartError(queuedId, "exception", juce::String::fromUTF8(e.what()));
                    progressSample();
                    std::lock_guard lock(stateMutex);
                    ++state.failed;
                    state.lastError = juce::String::fromUTF8(e.what());
                }
            }
        };
        processQueued();
        if (mode == Mode::Folder && ! cancelled)
        {
            setStage(SmartStage::Extracting, "Analyzing catalogued files in folder");
            const auto ids = repo.idsInFolder(folderScope);
            {
                std::lock_guard lock(stateMutex);
                state.total = (int) ids.size();
            }
            for (auto id : ids)
            {
                if (cancelled || checkpoint()) break;
                try
                {
                    const bool ok = processSample(id, catalogPath);
                    progressSample();
                    if (! ok)
                    {
                        std::lock_guard lock(stateMutex);
                        ++state.failed;
                    }
                }
                catch (const std::exception& e)
                {
                    repo.markSmartError(id, "exception", juce::String::fromUTF8(e.what()));
                    progressSample();
                    std::lock_guard lock(stateMutex);
                    ++state.failed;
                    state.lastError = juce::String::fromUTF8(e.what());
                }
            }
        }
        if (mode == Mode::Incremental && ! cancelled)
        {
            int staleBeforeVerify = 0;
            for (int pass = 0; pass < 2; ++pass)
            {
                setStage(SmartStage::Scanning, "Finding missing and stale metadata");
                int64_t lastId = 0;
                for (;;)
                {
                    if (checkpoint()) break;
                    processQueued();
                    const auto ids = repo.samplesNeedingSmartAnalysis(lastId, batchSize);
                    if (ids.empty()) break;
                    {
                        std::lock_guard lock(stateMutex);
                        state.total += (int) ids.size();
                    }
                    for (auto id : ids)
                    {
                        if (cancelled || checkpoint()) break;
                        try
                        {
                            const bool ok = processSample(id, catalogPath);
                            progressSample();
                            if (! ok)
                            {
                                std::lock_guard lock(stateMutex);
                                ++state.failed;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            repo.markSmartError(id, "exception", juce::String::fromUTF8(e.what()));
                            progressSample();
                            std::lock_guard lock(stateMutex);
                            ++state.failed;
                            state.lastError = juce::String::fromUTF8(e.what());
                        }
                    }
                    lastId = ids.back();
                }
                if (pass == 1) break;
                {
                    std::lock_guard lock(stateMutex);
                    staleBeforeVerify = state.stale;
                }
                setStage(SmartStage::Verifying, "Checking stored metadata against the files");
                lastId = 0;
                for (;;)
                {
                    if (checkpoint()) break;
                    const auto ids = repo.samplesWithSmartResultAfterId(lastId, batchSize);
                    if (ids.empty()) break;
                    for (auto id : ids)
                    {
                        if (cancelled || checkpoint()) break;
                        try { verifySample(id); }
                        catch (const std::exception& e)
                        {
                            std::lock_guard lock(stateMutex);
                            state.lastError = juce::String::fromUTF8(e.what());
                        }
                        std::lock_guard lock(stateMutex);
                        ++state.verified;
                    }
                    lastId = ids.back();
                }
                {
                    std::lock_guard lock(stateMutex);
                    // Nothing changed on disk: one pass was enough.
                    if (state.stale == staleBeforeVerify) break;
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(stateMutex);
        state.stage = SmartStage::Failed;
        state.message = juce::String("Smart analysis stopped: ") + juce::String::fromUTF8(e.what());
        state.lastError = juce::String::fromUTF8(e.what());
    }
    std::lock_guard lock(stateMutex);
    state.running = false;
    state.current.clear();
    if (cancelled)
    {
        state.cancelled = true;
        state.stage = SmartStage::Cancelled;
        state.message = "Smart analysis cancelled";
    }
    else if (state.stage != SmartStage::Failed)
    {
        state.stage = SmartStage::Complete;
        state.message = mode == Mode::Selected ? "Selected samples analyzed"
            : (mode == Mode::Folder ? "Folder analysis complete" : "Smart analysis complete");
    }
}
} // namespace sonicator
