#pragma once
#include "BatchTypes.h"
#include "core/scanner/FileScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <limits>
#include <memory>

namespace sonicator::batch
{
enum class ReadState { ok, eof, error, cancelled };

struct ReadResult
{
    ReadState state = ReadState::ok;
    int frames = 0;
    ErrorCode code = ErrorCode::none;
    juce::String error;
};

// One resolved stage. Global parameters are fixed when the stage is appended, so repeated reads
// are identical and a later step sees the real intermediate signal.
struct StageSpec
{
    enum class Kind { dc, trim, normalize, gain, fadeIn, fadeOut } kind = Kind::gain;
    std::array<double, 2> channelMeans { 0.0, 0.0 };
    int64_t trimBegin = 0;
    int64_t trimEnd = std::numeric_limits<int64_t>::max();
    int64_t fadeFrames = 0;
    int64_t totalFrames = 0;
    bool cosine = false;
    float gain = 1.0f;
};

// A replayable, bounded-memory rendering of the source through a fixed stage chain. One instance
// belongs to one worker and is never shared with the UI.
class PreparedAudio
{
public:
    PreparedAudio(std::unique_ptr<juce::AudioFormatReader>, int sampleRate, int channels,
                  int64_t frames, std::vector<StageSpec> stages,
                  std::vector<StepOutcome> outcomes = {}, CancelCheck cancel = {});
    ~PreparedAudio();
    PreparedAudio(const PreparedAudio&) = delete;
    PreparedAudio& operator=(const PreparedAudio&) = delete;
    int64_t frames() const { return length; }
    // First source frame that survives the trim chain, used by the honest A/B preview.
    int64_t startFrame() const { return firstFrame; }
    int sampleRate() const { return rate; }
    int channels() const { return channelCount; }
    const std::vector<StepOutcome>& stepOutcomes() const { return outcomes; }
    // At most `count` frames from `start`. Sequential calls continue the chain; a jump replays it.
    ReadResult readFrames(int64_t start, int count, juce::AudioBuffer<float>& destination);
    // Exact peak of the prepared stream, measured once in a full pass.
    ReadResult peak(float& value);
private:
    struct Runtime { int64_t index = 0; };
    void resetChain();
    ReadResult readBlock(int capacity, int& produced, CancelCheck);
    std::unique_ptr<juce::AudioFormatReader> reader;
    int rate = 0, channelCount = 0;
    int64_t sourceFrames = 0, length = 0, firstFrame = 0;
    std::vector<StageSpec> stages;
    std::vector<Runtime> runtime;
    std::vector<StepOutcome> outcomes;
    juce::AudioBuffer<float> working;
    juce::AudioBuffer<float> output;
    int64_t sourcePosition = 0, chainPosition = 0;
    bool chainStarted = false;
    bool peakKnown = false;
    float cachedPeak = 0.0f;
    CancelCheck cancelCheck;
};

struct PrepareAudioResult
{
    bool ok = false;
    ErrorCode code = ErrorCode::none;
    juce::String error;
    std::unique_ptr<PreparedAudio> audio;
    std::vector<Issue> warnings;
};

struct FrameRange
{
    bool active = false;
    int64_t begin = 0;
    int64_t end = 0;
};

// With a Split step, `range` is applied after the steps before it and the steps after it run on the
// part only. `stopAtSplit` prepares just the pre-split stream, which is what detection scans.
PrepareAudioResult prepareAudio(const SourceSnapshot&, const Recipe&, CancelCheck = {},
                                FrameRange range = {}, bool stopAtSplit = false);

// Fade coefficient for frame `n` of a stream of `total` frames. Fade out uses the same curve in
// reverse, so the final frame is exactly zero.
float fadeCoefficient(int64_t n, int64_t total, int64_t fadeFrames, bool fadeOut, bool cosine);
// Compensated (Neumaier) summation used by the DC analysis.
void compensatedAdd(double& sum, double& compensation, double value);
} // namespace sonicator::batch
