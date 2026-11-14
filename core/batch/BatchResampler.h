#pragma once
#include "BatchPipeline.h"
#include <memory>

struct SRC_STATE_tag;

namespace sonicator::batch
{
// Output conversion: stereo/mono mapping followed by continuous band-limited resampling. The
// wrapper produces exactly Nout = max(1, llround(N * Fout / Fin)) frames; the SRC filter tail is
// trimmed, and a documented short zero extension covers the EOF delay. A larger shortage is an
// error, never silently padded.
class ConvertedStream
{
public:
    ConvertedStream(std::unique_ptr<PreparedAudio>, int outputRate, int outputChannels,
                    CancelCheck = {});
    ~ConvertedStream();
    ConvertedStream(const ConvertedStream&) = delete;
    ConvertedStream& operator=(const ConvertedStream&) = delete;
    int64_t frames() const { return totalOut; }
    int sampleRate() const { return outRate; }
    int channels() const { return outChannels; }
    int inputSampleRate() const { return prepared != nullptr ? prepared->sampleRate() : 0; }
    // Sequential blocks; call again after a short block until eof.
    ReadResult readNext(int count, juce::AudioBuffer<float>& destination);
    float peak() const { return outputPeak; }
    const PreparedAudio* source() const { return prepared.get(); }
    static int64_t outputFrames(int64_t inputFrames, int inputRate, int outputRate);
    // Largest tolerated zero extension at EOF, in output frames.
    static int64_t maximumShortfall(int outputRate);
private:
    void fillPending(int capacity);
    bool loadInput();
    void convertChannels(const juce::AudioBuffer<float>& source, int frames);
    std::unique_ptr<PreparedAudio> prepared;
    int inRate = 0, inChannels = 0, outRate = 0, outChannels = 0;
    int64_t totalOut = 0;
    SRC_STATE_tag* src = nullptr;
    juce::AudioBuffer<float> raw, converted, pending;
    std::vector<float> feed, srOut;
    int feedOffset = 0, feedCount = 0;
    int pendingOffset = 0, pendingCount = 0;
    int64_t inputPosition = 0;
    bool sourceFinished = false;
    bool drained = false;
    bool finished = false;
    bool endOfInputSent = false;
    int64_t emitted = 0;
    float outputPeak = 0.0f;
    CancelCheck cancel;
    ReadResult failure;
};
} // namespace sonicator::batch
