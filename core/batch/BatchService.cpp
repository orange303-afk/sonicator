#include "BatchService.h"
#include "BatchEncoder.h"
#include "BatchPaths.h"
#include "BatchPipeline.h"
#include "BatchPublisher.h"
#include <algorithm>

namespace sonicator::batch
{
namespace
{
juce::var issueVar(const Issue& issue)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("code", errorCodeName(issue.code));
    object->setProperty("message", issue.message);
    object->setProperty("warning", issue.warning);
    object->setProperty("path", issue.path);
    return juce::var(object.release());
}

juce::var outputVar(const OutputResult& result)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("state", (int) result.state);
    object->setProperty("code", errorCodeName(result.code));
    object->setProperty("message", result.message);
    object->setProperty("source", result.source.getFullPathName());
    object->setProperty("target", result.target.getFullPathName());
    object->setProperty("format", formatLabel(result.format));
    object->setProperty("bits", result.bits);
    object->setProperty("floating", result.floating);
    object->setProperty("sampleRate", result.sampleRate);
    object->setProperty("channels", result.channels);
    object->setProperty("frames", (juce::int64) result.frames);
    object->setProperty("peak", result.peak);
    object->setProperty("startedMs", (juce::int64) result.startedMs);
    object->setProperty("finishedMs", (juce::int64) result.finishedMs);
    juce::Array<juce::var> warnings;
    for (const auto& warning : result.warnings) warnings.add(issueVar(warning));
    object->setProperty("warnings", warnings);
    juce::Array<juce::var> steps;
    for (const auto& step : result.steps)
    {
        auto stepObject = std::make_unique<juce::DynamicObject>();
        stepObject->setProperty("index", step.stepIndex);
        stepObject->setProperty("type", step.type);
        stepObject->setProperty("changed", step.changed);
        stepObject->setProperty("detail", step.detail);
        stepObject->setProperty("value", step.value);
        stepObject->setProperty("code", errorCodeName(step.code));
        steps.add(juce::var(stepObject.release()));
    }
    object->setProperty("steps", steps);
    return juce::var(object.release());
}

const char* stateName(OutputState state)
{
    switch (state)
    {
        case OutputState::completed: return "completed";
        case OutputState::skipped: return "skipped";
        case OutputState::failed: return "failed";
        case OutputState::cancelled: return "cancelled";
    }
    return "unknown";
}
} // namespace

BatchService::BatchService() : Thread("Sonicator batch processing") {}

BatchService::~BatchService()
{
    cancel();
    stopThread(10000);
}

bool BatchService::start(const PreparedBatch& prepared, juce::File folder)
{
    if (running.load()) return false;
    if (prepared.outputs.empty() || prepared.sources.empty()) return false;
    batch = prepared;
    reportFolder = std::move(folder);
    cancelled = false;
    {
        std::lock_guard lock(stateMutex);
        state = {};
        state.running = true;
        state.outputsTotal = (int) batch.outputs.size();
        state.phase = "Preparing";
    }
    running = true;
    startThread();
    return true;
}

void BatchService::cancel()
{
    cancelled = true;
}

Progress BatchService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}

Report BatchService::results() const
{
    std::lock_guard lock(stateMutex);
    return report;
}

void BatchService::run()
{
    Report local;
    local.jobId = batch.jobId;
    local.startedMs = juce::Time::currentTimeMillis();
    local.recipeFingerprint = recipeFingerprint(batch.request.recipe);
    local.skippedSources = batch.skipped;
    local.totalSources = (int) batch.sources.size();
    local.sources.resize(batch.sources.size());
    for (size_t i = 0; i < batch.sources.size(); ++i)
    {
        local.sources[i].sampleId = batch.sources[i].sampleId;
        local.sources[i].path = batch.sources[i].path;
    }
    for (const auto& plan : batch.outputs)
        if (plan.sourceIndex < local.sources.size()) ++local.sources[plan.sourceIndex].planned;

    const auto checkCancel = [this] { return cancelled.load(); };
    const auto protection = InputProtectionSet::from(batch);
    int done = 0;
    try
    {
        for (size_t index = 0; index < batch.outputs.size(); ++index)
        {
            const auto& plan = batch.outputs[index];
            const auto& source = batch.sources[plan.sourceIndex];
            OutputResult result;
            result.outputIndex = index;
            result.sampleId = source.sampleId;
            result.source = source.path;
            result.target = plan.target;
            result.format = plan.format;
            result.encoding = plan.encoding;
            result.bits = plan.bits;
            result.floating = plan.floating;
            result.sampleRate = plan.sampleRate;
            result.channels = plan.channels;
            result.startedMs = juce::Time::currentTimeMillis();
            {
                std::lock_guard lock(stateMutex);
                state.current = source.path.getFileName() + " -> " + formatLabel(plan.format);
                state.fraction = batch.outputs.empty()
                    ? 0.0
                    : (double) done / (double) batch.outputs.size();
            }
            if (checkCancel())
            {
                result.state = OutputState::cancelled;
                result.code = ErrorCode::cancelled;
                result.message = "Not started: the job was cancelled";
            }
            else if (plan.skipExisting)
            {
                result.state = OutputState::skipped;
                result.code = ErrorCode::conflict_skipped;
                result.message = "The output already exists and Skip was chosen";
            }
            else if (const auto free = freeBytesFor(plan.target.getParentDirectory());
                     free >= 0 && free < plan.estimatedBytes + spaceReserveBytes)
            {
                result.state = OutputState::failed;
                result.code = ErrorCode::insufficient_space;
                result.message = "Not enough free space for this output";
            }
            else
            {
                {
                    std::lock_guard lock(stateMutex);
                    state.phase = "Preparing";
                }
                auto preparedAudio = prepareAudio(source, batch.request.recipe, checkCancel);
                if (! preparedAudio.ok)
                {
                    result.state = checkCancel() ? OutputState::cancelled : OutputState::failed;
                    result.code = preparedAudio.code;
                    result.message = preparedAudio.error;
                }
                else
                {
                    const auto phase = [this](const juce::String& name)
                    {
                        std::lock_guard lock(stateMutex);
                        state.phase = name;
                    };
                    result = renderAndPublish(std::move(preparedAudio.audio), plan, source,
                                              batch.request.recipe, batch.request.conflicts,
                                              protection, checkCancel, phase);
                    result.outputIndex = index;
                }
            }
            result.finishedMs = juce::Time::currentTimeMillis();
            ++done;
            {
                std::lock_guard lock(stateMutex);
                state.outputsDone = done;
                state.fraction = (double) done / (double) std::max<size_t>(1, batch.outputs.size());
            }
            if (plan.sourceIndex < local.sources.size())
            {
                auto& summary = local.sources[plan.sourceIndex];
                switch (result.state)
                {
                    case OutputState::completed: ++summary.completed; ++local.completed; break;
                    case OutputState::skipped: ++summary.skipped; ++local.skipped; break;
                    case OutputState::failed: ++summary.failed; ++local.failed; break;
                    case OutputState::cancelled: ++summary.cancelled; ++local.cancelled; break;
                }
            }
            local.outputs.push_back(std::move(result));
            if (checkCancel()) local.abandoned = true;
        }
    }
    catch (const std::exception& error)
    {
        local.abandoned = true;
        local.skippedSources.push_back({ ErrorCode::encode_failed,
                                         juce::String("The worker stopped: ") + error.what(),
                                         false, {} });
    }
    for (size_t index = local.outputs.size(); index < batch.outputs.size(); ++index)
    {
        const auto& plan = batch.outputs[index];
        const auto& source = batch.sources[plan.sourceIndex];
        OutputResult missing;
        missing.outputIndex = index;
        missing.sampleId = source.sampleId;
        missing.source = source.path;
        missing.target = plan.target;
        missing.format = plan.format;
        missing.bits = plan.bits;
        missing.sampleRate = plan.sampleRate;
        missing.channels = plan.channels;
        missing.state = OutputState::cancelled;
        missing.code = ErrorCode::cancelled;
        missing.message = "The worker stopped before this output";
        missing.startedMs = juce::Time::currentTimeMillis();
        missing.finishedMs = missing.startedMs;
        if (plan.sourceIndex < local.sources.size()) ++local.sources[plan.sourceIndex].cancelled;
        ++local.cancelled;
        local.outputs.push_back(std::move(missing));
    }
    local.finishedMs = juce::Time::currentTimeMillis();
    if (reportFolder.getFullPathName().isNotEmpty())
    {
        try { saveReport(local, reportFolder); }
        catch (...) { /* a diagnostic copy is optional; the dialog still holds the report */ }
    }
    {
        std::lock_guard lock(stateMutex);
        report = std::move(local);
        state.running = false;
        state.cancelled = cancelled.load();
        state.phase = "Finished";
    }
    running = false;
}

juce::String BatchService::reportToJson(const Report& report)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("jobId", report.jobId);
    root->setProperty("startedMs", (juce::int64) report.startedMs);
    root->setProperty("finishedMs", (juce::int64) report.finishedMs);
    root->setProperty("recipeFingerprint", report.recipeFingerprint);
    root->setProperty("totalSources", report.totalSources);
    root->setProperty("completed", report.completed);
    root->setProperty("skipped", report.skipped);
    root->setProperty("failed", report.failed);
    root->setProperty("cancelled", report.cancelled);
    root->setProperty("cancelledEarly", report.abandoned);
    juce::Array<juce::var> sources;
    for (const auto& source : report.sources)
    {
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty("sampleId", (juce::int64) source.sampleId);
        object->setProperty("path", source.path.getFullPathName());
        object->setProperty("planned", source.planned);
        object->setProperty("completed", source.completed);
        object->setProperty("failed", source.failed);
        object->setProperty("skipped", source.skipped);
        object->setProperty("cancelled", source.cancelled);
        sources.add(juce::var(object.release()));
    }
    root->setProperty("sources", sources);
    juce::Array<juce::var> skipped;
    for (const auto& issue : report.skippedSources) skipped.add(issueVar(issue));
    root->setProperty("skippedSources", skipped);
    juce::Array<juce::var> outputs;
    for (const auto& output : report.outputs) outputs.add(outputVar(output));
    root->setProperty("outputs", outputs);
    return juce::JSON::toString(juce::var(root.release()), false);
}

juce::String BatchService::reportToText(const Report& report)
{
    juce::String text;
    text << "Batch Processing report\n"
         << "Job: " << report.jobId << "\n"
         << "Sources: " << report.totalSources << ", completed outputs: " << report.completed
         << ", skipped: " << report.skipped << ", failed: " << report.failed
         << ", cancelled: " << report.cancelled << "\n\n";
    for (const auto& result : report.outputs)
    {
        text << stateName(result.state) << " | " << result.source.getFileName() << " -> "
             << result.target.getFullPathName() << " | " << formatLabel(result.format) << " "
             << result.bits << "-bit " << result.sampleRate << " Hz " << result.channels << " ch";
        if (result.frames > 0) text << " | " << result.frames << " frames";
        if (result.code != ErrorCode::none) text << " | " << errorCodeName(result.code);
        if (result.message.isNotEmpty()) text << " | " << result.message;
        text << "\n";
        for (const auto& warning : result.warnings)
            text << "  warning: " << warning.message << "\n";
    }
    if (! report.skippedSources.empty())
    {
        text << "\nSkipped sources:\n";
        for (const auto& issue : report.skippedSources)
            text << "  " << issue.path << " | " << errorCodeName(issue.code) << " | "
                 << issue.message << "\n";
    }
    return text;
}

juce::File BatchService::saveReport(const Report& report, const juce::File& folder, int maxFiles,
                                    int maxAgeDays)
{
    folder.createDirectory();
    const auto file = folder.getChildFile("batch-" + report.jobId + ".json");
    juce::TemporaryFile temporary(file);
    if (! temporary.getFile().replaceWithText(reportToJson(report), false, false, "\n"))
        throw std::runtime_error("Cannot write the batch report");
    if (! temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Cannot publish the batch report");

    const auto now = juce::Time::currentTimeMillis();
    std::vector<juce::File> reports;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "batch-*.json",
                                                          juce::File::findFiles))
        reports.push_back(entry.getFile());
    std::sort(reports.begin(), reports.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });
    for (size_t i = 0; i < reports.size(); ++i)
    {
        const auto ageDays = (double) (now - reports[i].getLastModificationTime().toMilliseconds())
            / (1000.0 * 60.0 * 60.0 * 24.0);
        if ((int) i >= maxFiles || ageDays > (double) maxAgeDays) reports[i].deleteFile();
    }
    return file;
}
} // namespace sonicator::batch
