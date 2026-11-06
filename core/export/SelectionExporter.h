#pragma once
#include "AudioExporter.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace sonicator {
// Prepares one selection export at a time away from the message thread. A newer prepare()
// invalidates the previous generation so a stale result is never exposed. The owner polls
// state() (the preview uses its existing 30 Hz timer) or calls waitUntilFinished().
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
    explicit SelectionExporter(juce::File exportFolder);
    ~SelectionExporter();
    void prepare(const juce::File& source, double start, double end);
    void cancel();
    State state() const;
    // Joins the current job if it has already finished; returns false on timeout.
    bool waitUntilFinished(int timeoutMs);
private:
    juce::File folder;
    mutable std::mutex stateMutex;
    std::condition_variable finishedCondition;
    State current;
    juce::File requestSource;
    double requestStart = 0, requestEnd = 0;
    juce::uint64 generation = 0;
    bool jobFinished = true;
    std::thread worker;
    void run(juce::uint64 jobGeneration);
};
} // namespace sonicator
