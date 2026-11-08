#include "SilenceDetector.h"
#include <algorithm>
#include <cmath>
namespace sonicator {
SplitDetection detectSplitParts(juce::AudioFormatReader& reader, const SilenceOptions& options,
                                CancelCheck cancel)
{
    SplitDetection result;
    const auto rate = reader.sampleRate;
    const auto total = reader.lengthInSamples;
    if (! (rate > 0) || total <= 0 || reader.numChannels < 1) return result;

    const auto grainFrames = std::max<juce::int64>(1, (juce::int64) std::llround(rate * 0.001));
    const auto threshold = (float) std::pow(10.0, options.thresholdDb / 20.0);
    const auto minSilenceFrames = (juce::int64) std::llround(rate * options.minSilenceMs / 1000.0);
    const auto minPartFrames = (juce::int64) std::llround(rate * options.minPartMs / 1000.0);
    const auto padFrames = (juce::int64) std::llround(rate * options.edgePaddingMs / 1000.0);
    const int channels = (int) reader.numChannels;

    struct Gap { juce::int64 start = 0, end = 0; };
    std::vector<Gap> gaps;
    juce::int64 silenceStart = -1;
    juce::int64 grainStart = 0;

    // Blocks are a whole number of grains, so every block boundary is also a grain boundary and
    // a grain never needs data from two buffers.
    const auto blockFrames = grainFrames * 1024;
    juce::AudioBuffer<float> buffer(channels, (int) std::min<juce::int64>(blockFrames, total));
    const auto processGrain = [&](juce::int64 start, juce::int64 end, juce::int64 blockStart)
    {
        float peak = 0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* data = buffer.getReadPointer(ch);
            for (auto n = start; n < end; ++n)
                peak = std::max(peak, std::abs(data[n - blockStart]));
        }
        if (peak <= threshold)
        {
            if (silenceStart < 0) silenceStart = start;
        }
        else if (silenceStart >= 0)
        {
            if (start - silenceStart >= minSilenceFrames)
                gaps.push_back({ silenceStart, start });
            silenceStart = -1;
        }
    };

    for (juce::int64 blockStart = 0; blockStart < total; blockStart += blockFrames)
    {
        if (cancel && cancel()) return result;
        const int count = (int) std::min<juce::int64>(blockFrames, total - blockStart);
        buffer.setSize(channels, count, false, false, true);
        if (! reader.read(buffer.getArrayOfWritePointers(), channels, blockStart, count))
            return result;
        const auto blockEnd = blockStart + count;
        while (grainStart + grainFrames <= blockEnd)
        {
            processGrain(grainStart, grainStart + grainFrames, blockStart);
            grainStart += grainFrames;
        }
    }
    if (grainStart < total && ! (cancel && cancel()))
    {
        const auto blockStart = (grainStart / blockFrames) * blockFrames;
        const int count = (int) std::min<juce::int64>(blockFrames, total - blockStart);
        buffer.setSize(channels, count, false, false, true);
        if (reader.read(buffer.getArrayOfWritePointers(), channels, blockStart, count))
            processGrain(grainStart, total, blockStart);
    }
    if (silenceStart >= 0 && total - silenceStart >= minSilenceFrames)
        gaps.push_back({ silenceStart, total });

    struct Range { juce::int64 start = 0, end = 0; };
    std::vector<Range> ranges;
    juce::int64 cursor = 0;
    for (const auto& gap : gaps)
    {
        if (gap.start > cursor) ranges.push_back({ cursor, gap.start });
        cursor = std::max(cursor, gap.end);
    }
    if (cursor < total) ranges.push_back({ cursor, total });

    result.parts.reserve(ranges.size());
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        auto start = ranges[i].start;
        auto end = ranges[i].end;
        const auto previousEnd = i == 0 ? juce::int64(0) : ranges[i - 1].end;
        const auto nextStart = i + 1 < ranges.size() ? ranges[i + 1].start : total;
        start = std::max(previousEnd, start - padFrames);
        end = std::min(nextStart, end + padFrames);
        if (end <= start) continue;
        if (end - start < minPartFrames)
        {
            ++result.discardedShortParts;
            continue;
        }
        result.parts.push_back({ (double) start / rate, (double) end / rate });
    }
    return result;
}
} // namespace sonicator
