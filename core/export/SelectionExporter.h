#pragma once
#include "AudioExporter.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace sonicator {
// One persistent background worker prepares selection exports. prepare() never joins the message
// thread; a newer request invalidates the running generation and is picked up next. The owner polls
// state() (the preview uses its 30 Hz timer) or calls waitUntilFinished().
class SelectionExporter
{
public:
    enum class Status { idle, preparing, ready, failed };
    struct State
    {
        Status status = Status::idle;
        juce::File file;
        juce::String error;
        double start = 0, end = 0;
        juce::uint64 generation = 0;
        bool isReady() const { return status == Status::ready && file.existsAsFile(); }
    };
    using ExportFunction = std::function<ExportResult(const juce::File&, double, double,
                                                      const juce::File&, CancelCheck)>;
    explicit SelectionExporter(juce::File exportFolder, ExportFunction exportFunction = {});
    ~SelectionExporter();
    void prepare(const juce::File& source, double start, double end);
    void cancel();
    State state() const;
    // Waits for the current generation to finish; returns false on timeout.
    bool waitUntilFinished(int timeoutMs);
private:
    struct Request
    {
        juce::File source;
        double start = 0, end = 0;
        juce::uint64 generation = 0;
    };
    juce::File folder;
    ExportFunction exporter;
    mutable std::mutex stateMutex;
    std::condition_variable workCondition, finishedCondition;
    State current;
    Request pendingRequest;
    bool hasPendingRequest = false, stopping = false, jobFinished = true;
    juce::uint64 generation = 0;
    std::thread worker;
    void run();
};
} // namespace sonicator
