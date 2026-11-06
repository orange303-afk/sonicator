#include "SelectionExporter.h"
namespace sonicator {
SelectionExporter::SelectionExporter(juce::File exportFolder) : folder(std::move(exportFolder))
{
}
SelectionExporter::~SelectionExporter()
{
    {
        std::lock_guard lock(stateMutex);
        ++generation;
    }
    if (worker.joinable()) worker.join();
}
void SelectionExporter::prepare(const juce::File& source, double start, double end)
{
    juce::uint64 jobGeneration = 0;
    {
        std::lock_guard lock(stateMutex);
        jobGeneration = ++generation;
    }
    // The old worker checks the generation at every chunk boundary, so this join stays short.
    if (worker.joinable()) worker.join();
    {
        std::lock_guard lock(stateMutex);
        jobFinished = false;
        requestSource = source;
        requestStart = start;
        requestEnd = end;
        current = {};
        current.status = Status::preparing;
        current.start = start;
        current.end = end;
        current.generation = jobGeneration;
    }
    worker = std::thread([this, jobGeneration] { run(jobGeneration); });
}
void SelectionExporter::cancel()
{
    std::lock_guard lock(stateMutex);
    ++generation;
    current = {};
}
SelectionExporter::State SelectionExporter::state() const
{
    std::lock_guard lock(stateMutex);
    return current;
}
bool SelectionExporter::waitUntilFinished(int timeoutMs)
{
    bool finished = false;
    {
        std::unique_lock lock(stateMutex);
        finished = finishedCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                              [this] { return jobFinished; });
    }
    if (finished && worker.joinable()) worker.join();
    return finished;
}
void SelectionExporter::run(juce::uint64 jobGeneration)
{
    juce::File source;
    double start = 0, end = 0;
    {
        std::lock_guard lock(stateMutex);
        source = requestSource;
        start = requestStart;
        end = requestEnd;
    }
    const auto cancel = [this, jobGeneration]
    {
        std::lock_guard lock(stateMutex);
        return jobGeneration != generation;
    };
    const auto result = AudioExporter().exportSelection(source, start, end, folder, cancel);
    {
        std::lock_guard lock(stateMutex);
        if (jobGeneration == generation)
        {
            current.status = result.success ? Status::ready : Status::failed;
            current.file = result.file;
            current.error = result.error;
        }
        jobFinished = true;
    }
    finishedCondition.notify_all();
}
} // namespace sonicator
