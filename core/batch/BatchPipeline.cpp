#include "BatchPipeline.h"
#include "core/audio/SonicatorFormats.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sonicator::batch
{
namespace
{
ReadResult cancelledResult()
{
    return { ReadState::cancelled, 0, ErrorCode::cancelled, "Cancelled" };
}
ReadResult errorResult(ErrorCode code, juce::String message)
{
    return { ReadState::error, 0, code, std::move(message) };
}

std::unique_ptr<juce::AudioFormatReader> openReader(const juce::File& file)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}

int64_t clampFrames(int64_t value, int64_t low, int64_t high)
{
    return std::min(std::max(value, low), high);
}

struct Analysis
{
    int64_t count = 0;
    std::array<double, 2> sums { 0.0, 0.0 };
    std::array<double, 2> compensation { 0.0, 0.0 };
    float peak = 0.0f;
    int64_t first = -1, last = -1;
};

ReadResult analyzeStream(PreparedAudio& audio, double thresholdDb, bool wantBounds,
                         Analysis& analysis, CancelCheck cancel)
{
    analysis = {};
    const auto threshold = std::pow(10.0, thresholdDb / 20.0);
    juce::AudioBuffer<float> block(audio.channels(), (int) blockFrames);
    int64_t position = 0;
    while (position < audio.frames())
    {
        if (cancel && cancel()) return cancelledResult();
        const auto wanted = (int) std::min<int64_t>(blockFrames, audio.frames() - position);
        const auto result = audio.readFrames(position, wanted, block);
        if (result.state != ReadState::ok || result.frames <= 0) return result;
        for (int n = 0; n < result.frames; ++n)
        {
            double frameMaximum = 0.0;
            for (int channel = 0; channel < audio.channels(); ++channel)
            {
                const double value = block.getSample(channel, n);
                compensatedAdd(analysis.sums[(size_t) channel],
                               analysis.compensation[(size_t) channel], value);
                analysis.peak = std::max(analysis.peak, (float) std::abs(value));
                frameMaximum = std::max(frameMaximum, std::abs(value));
            }
            if (wantBounds)
            {
                const auto index = position + n;
                if (frameMaximum > threshold)
                {
                    if (analysis.first < 0) analysis.first = index;
                    analysis.last = index;
                }
            }
        }
        analysis.count += result.frames;
        position += result.frames;
    }
    return { ReadState::ok, 0, ErrorCode::none, {} };
}

double compensatedTotal(double sum, double compensation)
{
    return sum + compensation;
}

juce::String describeMeans(const std::array<double, 2>& means, int channels)
{
    juce::String text = juce::String(means[0], 7);
    if (channels > 1) text += ", " + juce::String(means[1], 7);
    return text;
}
} // namespace

void compensatedAdd(double& sum, double& compensation, double value)
{
    const double t = sum + value;
    if (std::abs(sum) >= std::abs(value)) compensation += (sum - t) + value;
    else compensation += (value - t) + sum;
    sum = t;
}

float fadeCoefficient(int64_t n, int64_t total, int64_t fadeFrames, bool fadeOut, bool cosine)
{
    if (fadeFrames < 2) return 1.0f;
    double t = 1.0;
    if (fadeOut)
    {
        if (n >= total - fadeFrames) t = (double) (total - 1 - n) / (double) (fadeFrames - 1);
    }
    else if (n < fadeFrames)
    {
        t = (double) n / (double) (fadeFrames - 1);
    }
    t = juce::jlimit(0.0, 1.0, t);
    const double gain = cosine ? (1.0 - std::cos(juce::MathConstants<double>::pi * t)) / 2.0 : t;
    return (float) gain;
}

PreparedAudio::PreparedAudio(std::unique_ptr<juce::AudioFormatReader> input, int sampleRate,
                             int channels, int64_t frames, std::vector<StageSpec> stageSpecs,
                             std::vector<StepOutcome> stepOutcomes)
    : reader(std::move(input)), rate(sampleRate), channelCount(channels), sourceFrames(frames),
      stages(std::move(stageSpecs)), outcomes(std::move(stepOutcomes))
{
    runtime.resize(stages.size());
    working.setSize(std::max(1, channelCount), (int) blockFrames);
    output.setSize(std::max(1, channelCount), (int) blockFrames);
    length = sourceFrames;
    for (const auto& stage : stages)
    {
        if (stage.kind != StageSpec::Kind::trim) continue;
        const auto begin = clampFrames(stage.trimBegin, 0, length);
        const auto end = clampFrames(stage.trimEnd, 0, length);
        firstFrame += begin;
        length = std::max<int64_t>(0, end - begin);
    }
}

PreparedAudio::~PreparedAudio() = default;

void PreparedAudio::resetChain()
{
    sourcePosition = 0;
    chainPosition = 0;
    chainStarted = true;
    for (auto& state : runtime) state.index = 0;
}

ReadResult PreparedAudio::readBlock(int capacity, int& produced, CancelCheck cancel)
{
    produced = 0;
    if (cancel && cancel()) return cancelledResult();
    if (reader == nullptr) return errorResult(ErrorCode::decode_failed, "The source reader is closed");
    while (produced < capacity && sourcePosition < sourceFrames)
    {
        const int originalCount = (int) std::min<int64_t>(
            std::min<int64_t>(blockFrames, sourceFrames - sourcePosition), capacity - produced);
        working.clear();
        if (! reader->read(&working, 0, originalCount, sourcePosition, true, true))
            return errorResult(ErrorCode::decode_failed, "The source could not be decoded");
        for (int channel = 0; channel < channelCount; ++channel)
            for (int n = 0; n < originalCount; ++n)
                if (! std::isfinite(working.getSample(channel, n)))
                    return errorResult(ErrorCode::decode_failed,
                                       "The source contains NaN or infinite samples");
        sourcePosition += originalCount;
        int count = originalCount;
    for (size_t i = 0; i < stages.size(); ++i)
    {
        const auto& stage = stages[i];
        auto& state = runtime[i];
        const int64_t base = state.index;
        const int inputCount = count;
        switch (stage.kind)
        {
            case StageSpec::Kind::trim:
            {
                int kept = 0;
                for (int channel = 0; channel < channelCount; ++channel)
                {
                    auto* samples = working.getWritePointer(channel);
                    int write = 0;
                    for (int n = 0; n < count; ++n)
                    {
                        const auto index = base + n;
                        if (index >= stage.trimBegin && index < stage.trimEnd)
                            samples[write++] = samples[n];
                    }
                    if (channel + 1 == channelCount) kept = write;
                }
                count = kept;
                break;
            }
            case StageSpec::Kind::dc:
                for (int channel = 0; channel < channelCount; ++channel)
                {
                    const auto mean = (float) stage.channelMeans[(size_t) std::min(channel, 1)];
                    auto* samples = working.getWritePointer(channel);
                    for (int n = 0; n < count; ++n) samples[n] -= mean;
                }
                break;
            case StageSpec::Kind::gain:
            case StageSpec::Kind::normalize:
                for (int channel = 0; channel < channelCount; ++channel)
                {
                    auto* samples = working.getWritePointer(channel);
                    for (int n = 0; n < count; ++n) samples[n] *= stage.gain;
                }
                break;
            case StageSpec::Kind::fadeIn:
            case StageSpec::Kind::fadeOut:
            {
                const bool fadeOut = stage.kind == StageSpec::Kind::fadeOut;
                for (int n = 0; n < count; ++n)
                {
                    const auto gain = fadeCoefficient(base + n, stage.totalFrames,
                                                     stage.fadeFrames, fadeOut, stage.cosine);
                    if (gain == 1.0f) continue;
                    for (int channel = 0; channel < channelCount; ++channel)
                        working.getWritePointer(channel)[n] *= gain;
                }
                break;
            }
        }
        state.index += inputCount;
    }
    for (int channel = 0; channel < channelCount; ++channel)
        for (int n = 0; n < count; ++n)
        {
            const auto value = working.getSample(channel, n);
            if (! std::isfinite(value))
                return errorResult(ErrorCode::decode_failed,
                                   "Processing produced NaN or infinite samples");
            output.setSample(channel, produced + n, value);
        }
        produced += count;
    }
    if (produced == 0) return { ReadState::eof, 0, ErrorCode::none, {} };
    return { ReadState::ok, produced, ErrorCode::none, {} };
}

ReadResult PreparedAudio::readFrames(int64_t start, int count, juce::AudioBuffer<float>& destination)
{
    if (start < 0 || count < 0) return errorResult(ErrorCode::decode_failed, "Invalid read range");
    if (count == 0) return { ReadState::ok, 0, ErrorCode::none, {} };
    if (start >= length) return { ReadState::eof, 0, ErrorCode::none, {} };
    if (! chainStarted || chainPosition != start) resetChain();
    juce::AudioBuffer<float> scratch;
    while (chainPosition < start)
    {
        const auto wanted = (int) std::min<int64_t>(blockFrames, start - chainPosition);
        scratch.setSize(std::max(1, channelCount), wanted, false, false, true);
        int produced = 0;
        const auto result = readBlock(wanted, produced, {});
        if (result.state != ReadState::ok) return result;
        if (produced <= 0) return { ReadState::eof, 0, ErrorCode::none, {} };
        chainPosition += produced;
    }
    const int wanted = (int) std::min<int64_t>(
        std::min<int64_t>(count, length - chainPosition), blockFrames);
    int produced = 0;
    const auto result = readBlock(wanted, produced, {});
    if (result.state != ReadState::ok) return result;
    if (produced <= 0) return { ReadState::eof, 0, ErrorCode::none, {} };
    destination.setSize(std::max(1, channelCount), produced, false, false, true);
    for (int channel = 0; channel < channelCount; ++channel)
        destination.copyFrom(channel, 0, output, channel, 0, produced);
    chainPosition += produced;
    return { ReadState::ok, produced, ErrorCode::none, {} };
}

ReadResult PreparedAudio::peak(float& value)
{
    if (! peakKnown)
    {
        Analysis analysis;
        const auto result = analyzeStream(*this, -400.0, false, analysis, {});
        if (result.state != ReadState::ok) return result;
        cachedPeak = analysis.peak;
        peakKnown = true;
        resetChain();
    }
    value = cachedPeak;
    return { ReadState::ok, 0, ErrorCode::none, {} };
}

PrepareAudioResult prepareAudio(const SourceSnapshot& source, const Recipe& recipe, CancelCheck cancel)
{
    PrepareAudioResult result;
    auto fail = [&result](ErrorCode code, juce::String message)
    {
        result.ok = false;
        result.code = code;
        result.error = std::move(message);
        return std::move(result);
    };
    if (cancel && cancel()) return fail(ErrorCode::cancelled, "Cancelled");
    auto reader = openReader(source.path);
    if (reader == nullptr) return fail(ErrorCode::decode_failed, "The source cannot be decoded");
    if ((int) std::lround(reader->sampleRate) != source.sampleRate
        || (int) reader->numChannels != source.channels
        || reader->lengthInSamples != source.frames)
        return fail(ErrorCode::source_changed, "The source changed after the batch snapshot");
    if (physicalIdentity(source.path) != source.identity)
        return fail(ErrorCode::source_changed, "The source changed after the batch snapshot");

    const auto rate = source.sampleRate;
    std::vector<StageSpec> specs;
    std::vector<StepOutcome> outcomes;
    int64_t currentLength = source.frames;
    auto makeAudio = [&](const std::vector<StageSpec>& chain, std::vector<StepOutcome> steps)
    {
        auto fresh = openReader(source.path);
        if (fresh == nullptr) return std::unique_ptr<PreparedAudio>();
        return std::make_unique<PreparedAudio>(std::move(fresh), source.sampleRate,
                                               source.channels, source.frames, chain,
                                               std::move(steps));
    };

    for (size_t index = 0; index < recipe.steps.size(); ++index)
    {
        const auto& step = recipe.steps[index];
        StepOutcome outcome;
        outcome.stepIndex = (int) index;
        outcome.type = stepTypeName(step.parameters);
        if (! step.enabled)
        {
            outcome.detail = "Disabled";
            outcomes.push_back(outcome);
            continue;
        }
        if (std::holds_alternative<RemoveDc>(step.parameters))
        {
            auto audio = makeAudio(specs, {});
            if (audio == nullptr) return fail(ErrorCode::decode_failed, "The source cannot be decoded");
            Analysis analysis;
            const auto measured = analyzeStream(*audio, -400.0, false, analysis, cancel);
            if (measured.state != ReadState::ok)
                return fail(measured.code, measured.error);
            const auto frames = std::max<int64_t>(1, analysis.count);
            StageSpec spec;
            spec.kind = StageSpec::Kind::dc;
            spec.channelMeans[0] = compensatedTotal(analysis.sums[0], analysis.compensation[0])
                / (double) frames;
            spec.channelMeans[1] = source.channels > 1
                ? compensatedTotal(analysis.sums[1], analysis.compensation[1]) / (double) frames
                : 0.0;
            outcome.changed = std::abs(spec.channelMeans[0]) > 1.0e-9
                || std::abs(spec.channelMeans[1]) > 1.0e-9;
            outcome.value = std::max(std::abs(spec.channelMeans[0]),
                                     std::abs(spec.channelMeans[1]));
            outcome.detail = "Mean " + describeMeans(spec.channelMeans, source.channels);
            specs.push_back(spec);
        }
        else if (auto* trim = std::get_if<TrimSilence>(&step.parameters))
        {
            auto audio = makeAudio(specs, {});
            if (audio == nullptr) return fail(ErrorCode::decode_failed, "The source cannot be decoded");
            Analysis analysis;
            const auto measured = analyzeStream(*audio, trim->thresholdDb, true, analysis, cancel);
            if (measured.state != ReadState::ok) return fail(measured.code, measured.error);
            const auto padding = (int64_t) std::ceil(trim->paddingMs * (double) rate / 1000.0);
            int64_t begin = 0, end = currentLength;
            if (analysis.first < 0)
            {
                outcome.detail = "The prepared stream is silent; its length is unchanged";
                outcome.changed = false;
            }
            else
            {
                begin = std::max<int64_t>(0, analysis.first - padding);
                end = std::min<int64_t>(currentLength, analysis.last + 1 + padding);
                outcome.changed = begin > 0 || end < currentLength;
                outcome.detail = juce::String("Kept frames ") + juce::String(begin) + ".."
                    + juce::String(end);
            }
            outcome.value = (double) (end - begin);
            StageSpec spec;
            spec.kind = StageSpec::Kind::trim;
            spec.trimBegin = begin;
            spec.trimEnd = end;
            specs.push_back(spec);
            currentLength = std::max<int64_t>(0, end - begin);
        }
        else if (auto* normalize = std::get_if<Normalize>(&step.parameters))
        {
            auto audio = makeAudio(specs, {});
            if (audio == nullptr) return fail(ErrorCode::decode_failed, "The source cannot be decoded");
            Analysis analysis;
            const auto measured = analyzeStream(*audio, -400.0, false, analysis, cancel);
            if (measured.state != ReadState::ok) return fail(measured.code, measured.error);
            StageSpec spec;
            spec.kind = StageSpec::Kind::normalize;
            spec.gain = analysis.peak > 1.0e-6f
                ? (float) (std::pow(10.0, normalize->peakDb / 20.0) / analysis.peak)
                : 1.0f;
            outcome.changed = std::abs(spec.gain - 1.0f) > 1.0e-6f;
            outcome.value = analysis.peak;
            outcome.detail = juce::String("Peak ") + juce::String(analysis.peak, 6) + " -> "
                + juce::String(normalize->peakDb, 2) + " dBFS";
            specs.push_back(spec);
        }
        else if (auto* gain = std::get_if<Gain>(&step.parameters))
        {
            StageSpec spec;
            spec.kind = StageSpec::Kind::gain;
            spec.gain = (float) std::pow(10.0, gain->gainDb / 20.0);
            outcome.changed = gain->gainDb != 0.0;
            outcome.value = gain->gainDb;
            outcome.detail = juce::String(gain->gainDb, 2) + " dB";
            specs.push_back(spec);
        }
        else if (auto* fade = std::get_if<FadeIn>(&step.parameters))
        {
            StageSpec spec;
            spec.kind = StageSpec::Kind::fadeIn;
            spec.cosine = fade->cosine;
            spec.fadeFrames = std::min<int64_t>(std::llround(fade->ms * (double) rate / 1000.0),
                                                currentLength);
            spec.totalFrames = currentLength;
            outcome.changed = spec.fadeFrames >= 2;
            outcome.value = fade->ms;
            outcome.detail = spec.fadeFrames < 2 ? "Shorter than two frames; unchanged"
                                                 : juce::String("Frames ") + juce::String(spec.fadeFrames);
            specs.push_back(spec);
        }
        else if (auto* fade = std::get_if<FadeOut>(&step.parameters))
        {
            StageSpec spec;
            spec.kind = StageSpec::Kind::fadeOut;
            spec.cosine = fade->cosine;
            spec.fadeFrames = std::min<int64_t>(std::llround(fade->ms * (double) rate / 1000.0),
                                                currentLength);
            spec.totalFrames = currentLength;
            outcome.changed = spec.fadeFrames >= 2;
            outcome.value = fade->ms;
            outcome.detail = spec.fadeFrames < 2 ? "Shorter than two frames; unchanged"
                                                 : juce::String("Frames ") + juce::String(spec.fadeFrames);
            specs.push_back(spec);
        }
        outcomes.push_back(outcome);
    }

    auto finalReader = openReader(source.path);
    if (finalReader == nullptr) return fail(ErrorCode::decode_failed, "The source cannot be decoded");
    result.audio = std::make_unique<PreparedAudio>(std::move(finalReader), source.sampleRate,
                                                   source.channels, source.frames, std::move(specs),
                                                   std::move(outcomes));
    result.ok = true;
    return result;
}
} // namespace sonicator::batch
