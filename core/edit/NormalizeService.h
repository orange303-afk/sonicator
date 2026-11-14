#pragma once
#include "core/scanner/FileScanner.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <mutex>
#include <vector>

namespace sonicator
{
// Peak target of every Normalize command: -0.1 dBFS, the value fixed by the batch processing
// design (docs/superpowers/specs/2026-11-14-batch-audio-converter-design.md).
inline constexpr float normalizePeakTarget = 0.9885531f;

enum class NormalizeFileState { normalized, unchanged, skipped, failed, cancelled };

struct NormalizeFileResult
{
    juce::File source;
    // The replaced original, or the copy written for a format that cannot be overwritten.
    juce::File destination;
    NormalizeFileState state = NormalizeFileState::skipped;
    bool replacedOriginal = false;
    float peak = 0.0f;
    float gain = 1.0f;
    juce::String error;
};

struct NormalizeOptions
{
    // False always writes a "<stem> edit" copy; formats without a writer always get a WAV copy.
    bool overwriteOriginals = true;
};

struct NormalizeProgress
{
    bool running = false, cancelled = false;
    int total = 0, processed = 0, normalized = 0, unchanged = 0, skipped = 0, failed = 0;
    juce::String current, message;
};

// Empty when the file can take part; otherwise the reason shown before and after the run.
juce::String normalizeSkipReason(const juce::File&);

// One file, two streaming passes: measure the peak, then write a temporary (or copy) file with the
// gain applied and replace the original only after the result was reopened and verified. Memory
// stays at one chunk. Silent sources and files already on target produce `unchanged`, never a
// rewrite. Safe to call on any thread; never touches the message thread.
NormalizeFileResult normalizeFile(const juce::File& source, const NormalizeOptions&,
                                  CancelCheck cancel = {});

// Reads the whole file once and returns its peak; used by the tests and by the preflight check.
bool normalizeMeasurePeak(const juce::File& source, int& channels, float& peak, juce::String& error);

// One background job at a time; the owner polls progress() and reads results() after the thread
// stops. A failed file never stops the batch.
class NormalizeService final : private juce::Thread
{
public:
    NormalizeService() : Thread("Normalize files") {}
    ~NormalizeService() override;
    bool start(const std::vector<juce::File>& sources, const NormalizeOptions&);
    void cancel();
    NormalizeProgress progress() const;
    std::vector<NormalizeFileResult> results() const;
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
private:
    std::vector<juce::File> files;
    NormalizeOptions options;
    std::atomic<bool> cancelled { false };
    mutable std::mutex stateMutex;
    NormalizeProgress state;
    std::vector<NormalizeFileResult> fileResults;
    void run() override;
};
} // namespace sonicator
