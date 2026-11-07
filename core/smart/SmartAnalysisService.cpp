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
bool isAppleDouble(const juce::File& file) { return file.getFileName().startsWith("._"); }
void append(std::vector<SmartCandidate>& destination, std::vector<SmartCandidate> source)
{
    destination.insert(destination.end(), std::make_move_iterator(source.begin()),
                       std::make_move_iterator(source.end()));
}
} // namespace
SmartAnalysisService::~SmartAnalysisService()
{
    cancel();
    waitForThreadToExit(-1);
}
bool SmartAnalysisService::start()
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    cancelled = false;
    { std::lock_guard lock(pauseMutex); paused = false; }
    { std::lock_guard lock(stateMutex); state = {}; state.running = true; state.message = "Reading metadata"; }
    if (! startThread())
    {
        std::lock_guard lock(stateMutex); state.running = false;
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
std::vector<SmartCandidate> SmartAnalysisService::collectCandidates(const SampleRecord& record,
                                                                    juce::String* error)
{
    std::vector<SmartCandidate> candidates = extractNameCandidates(record.filePath);
    const juce::File file(record.filePath);
    const auto extension = file.getFileExtension().toLowerCase();
    juce::String structuralError;
    if (extension == ".wav")
    {
        auto wav = extractWavMetadata(file);
        append(candidates, std::move(wav.candidates));
        structuralError = wav.error;
    }
    else if (extension == ".aif" || extension == ".aiff" || extension == ".aifc")
    {
        auto aiff = extractAiffMetadata(file);
        append(candidates, std::move(aiff.candidates));
        structuralError = aiff.error;
    }
    else if (extension == ".rx2")
    {
        auto rex = extractRex2Metadata(file);
        append(candidates, std::move(rex.candidates));
        structuralError = rex.error;
    }
    const auto sidecar = file.getSiblingFile(file.getFileName() + ".asd");
    if (sidecar.existsAsFile() && ! isAppleDouble(sidecar))
    {
        auto asd = extractAsdMetadata(sidecar);
        append(candidates, std::move(asd.candidates));
        if (structuralError.isEmpty()) structuralError = asd.error;
    }
    if (error != nullptr) *error = structuralError;
    return candidates;
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
bool SmartAnalysisService::refreshSample(int64_t sampleId)
{
    if (isThreadRunning()) return false;
    auto record = repo.findById(sampleId);
    if (! record) return false;
    juce::String error;
    auto candidates = collectCandidates(*record, &error);
    auto result = resolveSmart(candidates, repo.smartOverrides(sampleId));
    repo.replaceSmartCandidates(sampleId, candidates);
    repo.setSmartResult(sampleId, result);
    repo.setSmartInputs(sampleId, currentInputs(*record));
    return true;
}
void SmartAnalysisService::run()
{
    try
    {
        int64_t lastId = 0;
        for (;;)
        {
            if (checkpoint()) break;
            const auto ids = repo.samplesNeedingSmartAnalysis(lastId, 64);
            if (ids.empty()) break;
            for (const auto id : ids)
            {
                if (checkpoint()) break;
                auto record = repo.findById(id);
                if (! record) continue;
                {
                    std::lock_guard lock(stateMutex);
                    state.current = record->fileName;
                }
                juce::String error;
                auto candidates = collectCandidates(*record, &error);
                const auto result = resolveSmart(candidates, repo.smartOverrides(id));
                repo.replaceSmartCandidates(id, candidates);
                repo.setSmartResult(id, result);
                repo.setSmartInputs(id, currentInputs(*record));
                {
                    std::lock_guard lock(stateMutex);
                    ++state.processed;
                    if (error.isNotEmpty()) ++state.failed;
                }
            }
            lastId = ids.back();
        }
    }
    catch (const std::exception& e)
    {
        std::lock_guard lock(stateMutex);
        state.message = juce::String("Smart analysis stopped: ") + e.what();
    }
    std::lock_guard lock(stateMutex);
    state.running = false;
    state.current.clear();
    if (cancelled) { state.cancelled = true; state.message = "Smart analysis cancelled"; }
    else if (state.message.isEmpty()) state.message = "Smart analysis complete";
}
} // namespace sonicator
