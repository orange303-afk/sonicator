#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
namespace sonicator {
struct ExtractOptions
{
    juce::File outputFolder; // used only when sameFolder is false
    bool sameFolder = true;
    bool normalize = false;
};
enum class ExtractFileState { extracted, failed, cancelled };
struct ExtractFileResult
{
    juce::File source, folder;
    int written = 0, empty = 0, silent = 0, failed = 0;
    ExtractFileState state = ExtractFileState::failed;
    juce::String error;
    juce::StringArray problems;
};
struct ExtractProgress
{
    bool running = false, cancelled = false;
    int totalFiles = 0, processedFiles = 0;
    int totalSamples = 0, processedSamples = 0;
    int written = 0, skippedEmpty = 0, skippedSilent = 0, failed = 0;
    double fraction = 0.0;
    juce::String current;
};
// `001 Kick.wav`: the ordinal is the entry's position in its source, so skipping empty or silent
// entries never renumbers the files that are written. Path separators and wildcards are replaced
// with underscores; text stays intact so non-ASCII sample names survive.
juce::String extractSampleFileName(int ordinal, const juce::String& name);
// One background extract job at a time, modelled on SplitService. Sources are opened read-only.
// Each source writes into "<stem> samples" beside itself or inside the chosen folder, taking the
// next free numeric suffix instead of overwriting. The owner polls progress() and reads results()
// after the thread stops. A cancelled batch keeps every WAV that was already finished.
class ExtractService final : private juce::Thread
{
public:
    ExtractService() : Thread("Extract samples") {}
    ~ExtractService() override;
    bool start(const std::vector<juce::File>& sources, const ExtractOptions&);
    void cancel();
    ExtractProgress progress() const;
    std::vector<ExtractFileResult> results() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
private:
    std::vector<juce::File> files;
    ExtractOptions options;
    std::atomic<bool> cancelled { false };
    mutable std::mutex stateMutex;
    ExtractProgress state;
    std::vector<ExtractFileResult> fileResults;
    void run() override;
    void updateProgress(const std::function<void(ExtractProgress&)>&);
};
} // namespace sonicator
