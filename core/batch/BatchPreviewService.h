#pragma once
#include "BatchTypes.h"
#include <atomic>
#include <mutex>

namespace sonicator::batch
{
struct PreviewMeasurements
{
    float peak = 0.0f;
    double dc = 0.0;
    int64_t frames = 0;
    int sampleRate = 0;
    int channels = 0;
};

struct PreviewResult
{
    bool ok = false;
    bool cancelled = false;
    ErrorCode code = ErrorCode::none;
    juce::String error;
    int generation = 0;
    juce::File dry, wet;
    int64_t sourceStartFrame = 0;
    PreviewMeasurements dryInfo, wetInfo;
    std::vector<Issue> warnings;
};

// Renders a short, honest A/B: the chain parameters are measured on the whole source in the
// worker, then a dry WAV float and a wet WAV float cover the same surviving source range. It owns
// no audio device; the owner auditions the files through the existing AudioEngine.
class BatchPreviewService final : private juce::Thread
{
public:
    BatchPreviewService();
    ~BatchPreviewService() override;
    bool start(const SourceSnapshot&, const Recipe&, int generation, juce::File folder = {});
    void cancel();
    bool isRunning() const { return running.load(); }
    // Moves the finished result out; later calls return an empty result.
    PreviewResult takeResult();
    // Deletes the files of the last result if the owner did not take them.
    void discardResult();
    bool waitUntilFinished(int timeoutMs) { return waitForThreadToExit(timeoutMs); }
    static int64_t previewFrameLimit(int sampleRate) { return (int64_t) sampleRate * 10; }
private:
    void run() override;
    SourceSnapshot source;
    Recipe recipe;
    int generation = 0;
    juce::File folder;
    std::atomic<bool> running { false }, cancelled { false };
    mutable std::mutex resultMutex;
    PreviewResult result;
};
} // namespace sonicator::batch
