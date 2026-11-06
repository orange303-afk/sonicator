#include "SelectionExporter.h"
namespace sonicator {
SelectionExporter::SelectionExporter(juce::File exportFolder, ExportFunction exportFunction)
    : folder(std::move(exportFolder)), exporter(std::move(exportFunction))
{
    worker = std::thread([this] { run(); });
}
SelectionExporter::~SelectionExporter()
{
    {
        std::lock_guard lock(stateMutex);
        stopping = true;
        hasPendingRequest = false;
        ++generation;
        jobFinished = true;
    }
    workCondition.notify_all();
    finishedCondition.notify_all();
    if (worker.joinable()) worker.join();
}
void SelectionExporter::prepare(const juce::File& source, double start, double end)
{
    {
        std::lock_guard lock(stateMutex);
        pendingRequest = { source, start, end, ++generation };
        hasPendingRequest = true;
        jobFinished = false;
        current = {};
        current.status = Status::preparing;
        current.start = start;
        current.end = end;
        current.generation = generation;
    }
    workCondition.notify_one();
}
void SelectionExporter::cancel()
{
    {
        std::lock_guard lock(stateMutex);
        ++generation;
        hasPendingRequest = false;
        current = {};
        jobFinished = true;
    }
    finishedCondition.notify_all();
}
SelectionExporter::State SelectionExporter::state() const
{
    std::lock_guard lock(stateMutex);
    return current;
}
bool SelectionExporter::waitUntilFinished(int timeoutMs)
{
    std::unique_lock lock(stateMutex);
    return finishedCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                      [this] { return jobFinished; });
}
void SelectionExporter::run()
{
    for (;;)
    {
        Request request;
        {
            std::unique_lock lock(stateMutex);
            workCondition.wait(lock, [this] { return stopping || hasPendingRequest; });
            if (stopping) return;
            request = pendingRequest;
            hasPendingRequest = false;
        }
        const auto jobGeneration = request.generation;
        const auto cancelled = [this, jobGeneration]
        {
            std::lock_guard lock(stateMutex);
            return stopping || jobGeneration != generation;
        };
        const auto result = exporter
            ? exporter(request.source, request.start, request.end, folder, cancelled)
            : AudioExporter().exportSelection(request.source, request.start, request.end, folder, cancelled);
        {
            std::lock_guard lock(stateMutex);
            if (jobGeneration == generation)
            {
                current.status = result.success ? Status::ready : Status::failed;
                current.file = result.file;
                current.error = result.error;
                jobFinished = true;
            }
        }
        finishedCondition.notify_all();
    }
}
} // namespace sonicator
