#include "BatchSplit.h"
#include <algorithm>

namespace sonicator::batch
{
const SplitSilence* recipeSplit(const Recipe& recipe)
{
    for (const auto& step : recipe.steps)
        if (step.enabled)
            if (auto* split = std::get_if<SplitSilence>(&step.parameters)) return split;
    return nullptr;
}

bool recipeHasSplit(const Recipe& recipe) { return recipeSplit(recipe) != nullptr; }

int recipeSplitIndex(const Recipe& recipe)
{
    for (size_t i = 0; i < recipe.steps.size(); ++i)
        if (recipe.steps[i].enabled && std::holds_alternative<SplitSilence>(recipe.steps[i].parameters))
            return (int) i;
    return -1;
}

BatchSplitDetection detectBatchParts(PreparedAudio& audio, const SplitSilence& options,
                                     CancelCheck cancel)
{
    BatchSplitDetection result;
    const auto rate = audio.sampleRate();
    const auto length = audio.frames();
    if (length <= 0 || rate <= 0)
    {
        result.parts.push_back({ 0, std::max<int64_t>(0, length) });
        return result;
    }
    const double threshold = std::pow(10.0, options.thresholdDb / 20.0);
    const auto silenceFrames = (int64_t) std::ceil(options.minSilenceMs * rate / 1000.0);
    const auto minPartFrames = (int64_t) std::ceil(options.minPartMs * rate / 1000.0);
    juce::AudioBuffer<float> block(audio.channels(), (int) blockFrames);
    int64_t begin = -1, lastSound = -1;
    auto closePart = [&](int64_t end)
    {
        if (begin < 0 || end <= begin) return;
        if (end - begin < minPartFrames) { ++result.discardedShortParts; return; }
        result.parts.push_back({ begin, end });
    };
    int64_t position = 0;
    while (position < length)
    {
        if (cancel && cancel()) return result;
        const auto read = audio.readFrames(position,
            (int) std::min<int64_t>(blockFrames, length - position), block);
        if (read.state != ReadState::ok || read.frames <= 0) break;
        for (int n = 0; n < read.frames; ++n)
        {
            float frameMaximum = 0.0f;
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                frameMaximum = std::max(frameMaximum, std::abs(block.getSample(channel, n)));
            const auto index = position + n;
            if (frameMaximum > (float) threshold)
            {
                if (begin < 0) begin = index;
                lastSound = index;
            }
            else if (begin >= 0 && index - lastSound >= silenceFrames)
            {
                closePart(lastSound + 1);
                begin = -1;
                lastSound = -1;
            }
        }
        position += read.frames;
    }
    if (begin >= 0) closePart(lastSound + 1);
    if (result.parts.size() < 2)
    {
        result.parts.clear();
        result.parts.push_back({ 0, length });
        result.split = false;
        return result;
    }
    result.split = true;
    return result;
}
} // namespace sonicator::batch
