#include "BatchResampler.h"
#include <samplerate.h>
#include <algorithm>
#include <cmath>

namespace sonicator::batch
{
namespace
{
ReadResult failureResult(ErrorCode code, juce::String message)
{
    return { ReadState::error, 0, code, std::move(message) };
}
} // namespace

int64_t ConvertedStream::outputFrames(int64_t inputFrames, int inputRate, int outputRate)
{
    int64_t out = 0;
    if (! checkedFrameMath(std::max<int64_t>(1, inputFrames), inputRate, outputRate, out)) return 0;
    return out;
}

int64_t ConvertedStream::maximumShortfall(int outputRate)
{
    return std::max<int64_t>(64, outputRate / 10);
}

ConvertedStream::ConvertedStream(std::unique_ptr<PreparedAudio> input, int outputRate,
                                 int outputChannels, CancelCheck cancelCheck)
    : prepared(std::move(input)), cancel(std::move(cancelCheck))
{
    if (prepared == nullptr)
    {
        failure = failureResult(ErrorCode::decode_failed, "No prepared audio");
        return;
    }
    inRate = prepared->sampleRate();
    inChannels = prepared->channels();
    outRate = outputRate;
    outChannels = outputChannels;
    if (outChannels < 1 || outChannels > 2 || inRate <= 0 || outRate <= 0)
    {
        failure = failureResult(ErrorCode::invalid_recipe, "Unsupported output rate or channels");
        return;
    }
    totalOut = outputFrames(prepared->frames(), inRate, outRate);
    if (totalOut <= 0)
    {
        failure = failureResult(ErrorCode::invalid_recipe, "The output length overflows");
        return;
    }
    raw.setSize(inChannels, (int) blockFrames);
    converted.setSize(outChannels, (int) blockFrames);
    pending.setSize(outChannels, (int) blockFrames);
    if (inRate != outRate)
    {
        feed.resize((size_t) blockFrames * (size_t) outChannels);
        srOut.resize((size_t) blockFrames * (size_t) outChannels);
        int error = 0;
        src = src_new(SRC_SINC_BEST_QUALITY, outChannels, &error);
        if (src == nullptr || error != 0)
        {
            if (src != nullptr) src_delete(src);
            src = nullptr;
            failure = failureResult(ErrorCode::encode_failed,
                                    "The sample-rate converter could not be created");
        }
    }
}

ConvertedStream::~ConvertedStream()
{
    if (src != nullptr) src_delete(src);
}

void ConvertedStream::convertChannels(const juce::AudioBuffer<float>& source, int frames)
{
    if (inChannels == outChannels)
    {
        for (int channel = 0; channel < outChannels; ++channel)
            converted.copyFrom(channel, 0, source, channel, 0, frames);
        return;
    }
    if (inChannels == 1 && outChannels == 2)
    {
        converted.copyFrom(0, 0, source, 0, 0, frames);
        converted.copyFrom(1, 0, source, 0, 0, frames);
        return;
    }
    if (inChannels == 2 && outChannels == 1)
    {
        auto* destination = converted.getWritePointer(0);
        const auto* left = source.getReadPointer(0);
        const auto* right = source.getReadPointer(1);
        for (int n = 0; n < frames; ++n)
            destination[n] = (float) (((double) left[n] + (double) right[n]) * 0.5);
    }
}

bool ConvertedStream::loadInput()
{
    if (prepared == nullptr || inputPosition >= prepared->frames()) return false;
    const auto wanted = (int) std::min<int64_t>(blockFrames, prepared->frames() - inputPosition);
    auto result = prepared->readFrames(inputPosition, wanted, raw);
    if (result.state != ReadState::ok)
    {
        if (result.state == ReadState::cancelled)
            failure = failureResult(ErrorCode::cancelled, result.error);
        else
            failure = failureResult(result.code, result.error);
        return false;
    }
    if (result.frames <= 0) return false;
    convertChannels(raw, result.frames);
    feedCount = result.frames;
    feedOffset = 0;
    inputPosition += result.frames;
    if (src != nullptr)
        for (int n = 0; n < feedCount; ++n)
            for (int channel = 0; channel < outChannels; ++channel)
                feed[(size_t) n * (size_t) outChannels + (size_t) channel]
                    = converted.getSample(channel, n);
    return true;
}

void ConvertedStream::fillPending(int capacity)
{
    pendingOffset = 0;
    pendingCount = 0;
    if (capacity <= 0) return;
    capacity = std::min(capacity, (int) blockFrames);
    if (src == nullptr)
    {
        int produced = 0;
        while (produced < capacity)
        {
            if (feedOffset >= feedCount)
            {
                if (inputPosition >= prepared->frames())
                {
                    sourceFinished = true;
                    break;
                }
                if (! loadInput()) break;
            }
            const int take = std::min(feedCount - feedOffset, capacity - produced);
            for (int channel = 0; channel < outChannels; ++channel)
                pending.copyFrom(channel, produced, converted, channel, feedOffset, take);
            feedOffset += take;
            produced += take;
        }
        pendingCount = produced;
        if (produced == 0) drained = true;
        return;
    }

    int produced = 0;
    const double ratio = (double) outRate / (double) inRate;
    while (produced < capacity && ! drained)
    {
        if (feedOffset >= feedCount)
        {
            if (inputPosition < prepared->frames())
            {
                if (! loadInput()) break;
            }
            else
            {
                sourceFinished = true;
            }
        }
        const bool finalBlock = sourceFinished && feedOffset >= feedCount;
        SRC_DATA data {};
        data.src_ratio = ratio;
        data.end_of_input = finalBlock ? 1 : 0;
        if (finalBlock)
        {
            data.data_in = nullptr;
            data.input_frames = 0;
        }
        else
        {
            data.data_in = feed.data() + (size_t) feedOffset * (size_t) outChannels;
            data.input_frames = feedCount - feedOffset;
        }
        data.data_out = srOut.data();
        data.output_frames = capacity - produced;
        const int status = src_process(src, &data);
        if (status != 0)
        {
            failure = failureResult(ErrorCode::encode_failed,
                                    juce::String("The sample-rate converter failed: ")
                                        + src_strerror(status));
            break;
        }
        if (! finalBlock) feedOffset += (int) data.input_frames_used;
        const int generated = (int) data.output_frames_gen;
        for (int n = 0; n < generated; ++n)
            for (int channel = 0; channel < outChannels; ++channel)
                pending.setSample(channel, produced + n,
                                  srOut[(size_t) (produced + n) * (size_t) outChannels
                                        + (size_t) channel]);
        produced += generated;
        if (finalBlock)
        {
            endOfInputSent = true;
            if (generated == 0) drained = true;
        }
        else if (generated == 0 && data.input_frames_used == 0)
        {
            break;
        }
    }
    pendingCount = produced;
    if (produced == 0 && sourceFinished && (src == nullptr || drained)) drained = true;
}

ReadResult ConvertedStream::readNext(int count, juce::AudioBuffer<float>& destination)
{
    if (failure.state == ReadState::error || failure.state == ReadState::cancelled) return failure;
    if (count <= 0) return { ReadState::ok, 0, ErrorCode::none, {} };
    if (finished || emitted >= totalOut) return { ReadState::eof, 0, ErrorCode::none, {} };
    if (cancel && cancel())
    {
        failure = failureResult(ErrorCode::cancelled, "Cancelled");
        return failure;
    }
    const int wanted = (int) std::min<int64_t>(count, totalOut - emitted);
    destination.setSize(std::max(1, outChannels), wanted, false, false, true);
    int produced = 0;
    while (produced < wanted)
    {
        if (pendingOffset >= pendingCount)
        {
            if (drained && sourceFinished) break;
            fillPending(wanted - produced);
            if (failure.state == ReadState::error || failure.state == ReadState::cancelled)
                return failure;
            if (pendingCount == 0)
            {
                if (drained) break;
                if (sourceFinished) break;
            }
            continue;
        }
        const int take = std::min(pendingCount - pendingOffset, wanted - produced);
        for (int channel = 0; channel < outChannels; ++channel)
            destination.copyFrom(channel, produced, pending, channel, pendingOffset, take);
        pendingOffset += take;
        produced += take;
    }
    const bool atEnd = (drained && pendingOffset >= pendingCount)
        || (sourceFinished && pendingOffset >= pendingCount && pendingCount == 0);
    if (atEnd && produced < wanted)
    {
        const auto shortage = (int64_t) wanted - produced;
        if (shortage > maximumShortfall(outRate))
        {
            failure = failureResult(ErrorCode::validate_failed,
                                    "The resampler produced too few frames");
            return failure;
        }
        for (int channel = 0; channel < outChannels; ++channel)
            destination.clear(channel, produced, wanted - produced);
        produced = wanted;
    }
    if (produced <= 0) return { ReadState::eof, 0, ErrorCode::none, {} };
    for (int channel = 0; channel < outChannels; ++channel)
        for (int n = 0; n < produced; ++n)
            outputPeak = std::max(outputPeak, std::abs(destination.getSample(channel, n)));
    emitted += produced;
    if (emitted >= totalOut) finished = true;
    return { ReadState::ok, produced, ErrorCode::none, {} };
}
} // namespace sonicator::batch
