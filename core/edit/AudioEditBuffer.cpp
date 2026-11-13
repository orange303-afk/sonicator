#include "AudioEditBuffer.h"
#include "core/audio/SonicatorFormats.h"
#include <algorithm>
#include <cmath>

namespace sonicator::edit
{
namespace
{
const std::vector<float>& emptyChannel()
{
    static const std::vector<float> empty;
    return empty;
}
// -60 dBFS, the threshold used to decide that a frame carries no audio.
constexpr float silenceThreshold = 0.001f;
// -0.1 dBFS, the headroom target shared with AudioExporter::exportSampleFile.
constexpr float normalizeTarget = 0.9885531f;
constexpr double trimMarginSeconds = 0.005;
}

const std::vector<float>& AudioEditBuffer::channel(int index) const
{
    if (index <= 0) return left;
    return right.empty() ? emptyChannel() : right;
}

std::optional<AudioEditBuffer> AudioEditBuffer::load(const juce::File& file, juce::String& error)
{
    error.clear();
    if (! file.existsAsFile())
    {
        error = "File not found";
        return std::nullopt;
    }
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file";
        return std::nullopt;
    }
    if (reader->numChannels > 2)
    {
        error = "Editing supports mono and stereo only";
        return std::nullopt;
    }
    if (reader->lengthInSamples <= 0)
    {
        error = "The file has no audio";
        return std::nullopt;
    }

    AudioEditBuffer buffer;
    buffer.sampleRateValue = (int) std::lround(reader->sampleRate);
    buffer.bitsPerSampleValue = (int) reader->bitsPerSample;
    buffer.floatingPointValue = reader->usesFloatingPointData;
    const auto total = reader->lengthInSamples;
    const auto channelCount = (int) reader->numChannels;
    constexpr int chunkFrames = 65536;
    juce::AudioBuffer<float> chunk(channelCount, chunkFrames);
    buffer.left.reserve((size_t) total);
    if (channelCount > 1) buffer.right.reserve((size_t) total);
    for (int64_t frame = 0; frame < total; frame += chunkFrames)
    {
        const auto count = (int) juce::jmin<int64_t>(chunkFrames, total - frame);
        if (! reader->read(&chunk, 0, count, frame, true, true))
        {
            error = "Cannot read audio data";
            return std::nullopt;
        }
        buffer.left.insert(buffer.left.end(), chunk.getReadPointer(0), chunk.getReadPointer(0) + count);
        if (channelCount > 1)
            buffer.right.insert(buffer.right.end(), chunk.getReadPointer(1), chunk.getReadPointer(1) + count);
    }

    // JUCE exposes the sampler chunk and cue points through the reader's metadata keys.
    const auto& meta = reader->metadataValues;
    if (meta.containsKey("MidiUnityNote")) buffer.markerData.rootKey = meta.getValue("MidiUnityNote", "60").getIntValue();
    if (meta.getValue("NumSampleLoops", "0").getIntValue() > 0)
    {
        buffer.markerData.hasLoop = true;
        buffer.markerData.loopStart = meta.getValue("Loop0Start", "0").getLargeIntValue();
        buffer.markerData.loopEndExclusive = meta.getValue("Loop0End", "0").getLargeIntValue() + 1;
    }
    const auto cues = meta.getValue("NumCuePoints", "0").getIntValue();
    for (int i = 0; i < cues; ++i)
        buffer.markerData.cuePositions.push_back(
            meta.getValue("Cue" + juce::String(i) + "Offset", "0").getLargeIntValue());

    buffer.history.push_back(buffer.currentState());
    return buffer;
}

AudioEditBuffer AudioEditBuffer::fromSampleAudio(const SampleAudio& audio, FileMarkers markers)
{
    AudioEditBuffer buffer;
    buffer.sampleRateValue = audio.sampleRate;
    buffer.bitsPerSampleValue = audio.bitsPerSample;
    buffer.floatingPointValue = audio.floatingPoint;
    buffer.markerData = std::move(markers);
    buffer.left = audio.left;
    buffer.right = audio.right;
    buffer.history.push_back(buffer.currentState());
    return buffer;
}

SampleAudioPtr AudioEditBuffer::snapshot() const
{
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = sampleRateValue;
    audio->channels = channels();
    audio->bitsPerSample = bitsPerSampleValue;
    audio->floatingPoint = floatingPointValue;
    audio->frames = frames();
    audio->left = left;
    if (channels() > 1) audio->right = right;
    return audio;
}

FrameRange AudioEditBuffer::resolve(const FrameRange& selection) const
{
    const auto total = frames();
    if (! selection.valid()) return { 0, total };
    const auto start = juce::jlimit<int64_t>(0, total, selection.start);
    const auto end = juce::jlimit<int64_t>(0, total, selection.end);
    return end > start ? FrameRange { start, end } : FrameRange { 0, total };
}

bool AudioEditBuffer::apply(const EditRequest& request)
{
    if (frames() == 0) return false;
    const auto range = resolve(request.range);
    auto eraseRange = [&range](std::vector<float>& data)
    {
        if (data.empty()) return;
        data.erase(data.begin() + range.start, data.begin() + range.end);
    };
    switch (request.operation)
    {
        case EditOperation::reverse:
            if (range.length() < 2) return false;
            for (auto* data : { &left, &right })
                if (! data->empty())
                    std::reverse(data->begin() + range.start, data->begin() + range.end);
            break;
        case EditOperation::deleteRegion:
            if (! range.valid()) return false;
            eraseRange(left);
            eraseRange(right);
            break;
        case EditOperation::cropToSelection:
            if (! range.valid()) return false;
            for (auto* data : { &left, &right })
            {
                if (data->empty()) continue;
                data->erase(data->begin() + range.end, data->end());
                data->erase(data->begin(), data->begin() + range.start);
            }
            break;
        case EditOperation::silenceRegion:
            if (! range.valid()) return false;
            for (auto* data : { &left, &right })
                if (! data->empty())
                    std::fill(data->begin() + range.start, data->begin() + range.end, 0.0f);
            break;
        case EditOperation::fadeIn:
        case EditOperation::fadeOut:
        {
            if (! range.valid()) return false;
            const auto length = (float) range.length();
            for (int c = 0; c < channels(); ++c)
            {
                auto& data = c == 0 ? left : right;
                for (int64_t i = range.start; i < range.end; ++i)
                {
                    const auto position = (float) (i - range.start) / length;
                    data[(size_t) i] *= request.operation == EditOperation::fadeIn
                        ? position : 1.0f - position;
                }
            }
            break;
        }
        case EditOperation::normalize:
        {
            if (! range.valid()) return false;
            float peak = 0.0f;
            for (int c = 0; c < channels(); ++c)
                for (int64_t i = range.start; i < range.end; ++i)
                    peak = juce::jmax(peak, std::abs(channel(c)[(size_t) i]));
            if (peak <= 0.0f) return false;
            const auto gain = normalizeTarget / peak;
            for (int c = 0; c < channels(); ++c)
            {
                auto& data = c == 0 ? left : right;
                for (int64_t i = range.start; i < range.end; ++i) data[(size_t) i] *= gain;
            }
            break;
        }
        case EditOperation::gain:
        {
            if (! range.valid()) return false;
            const auto gain = std::pow(10.0f, request.gainDb / 20.0f);
            if (gain == 1.0f) return false;
            for (int c = 0; c < channels(); ++c)
            {
                auto& data = c == 0 ? left : right;
                for (int64_t i = range.start; i < range.end; ++i) data[(size_t) i] *= gain;
            }
            break;
        }
        case EditOperation::toMono:
        {
            if (channels() == 1) return false;
            for (size_t i = 0; i < left.size(); ++i) left[i] = 0.5f * (left[i] + right[i]);
            right.clear();
            break;
        }
        case EditOperation::removeDcOffset:
        {
            if (! range.valid()) return false;
            for (int c = 0; c < channels(); ++c)
            {
                auto& data = c == 0 ? left : right;
                double sum = 0;
                for (int64_t i = range.start; i < range.end; ++i) sum += data[(size_t) i];
                const auto mean = (float) (sum / (double) range.length());
                for (int64_t i = range.start; i < range.end; ++i) data[(size_t) i] -= mean;
            }
            break;
        }
        case EditOperation::trimSilence:
        {
            int64_t first = -1, last = -1;
            for (int64_t i = 0; i < frames(); ++i)
            {
                bool audible = false;
                for (int c = 0; c < channels() && ! audible; ++c)
                    audible = std::abs(channel(c)[(size_t) i]) >= silenceThreshold;
                if (! audible) continue;
                if (first < 0) first = i;
                last = i;
            }
            if (first < 0) return false;
            const auto margin = (int64_t) std::lround(trimMarginSeconds * (double) sampleRateValue);
            const auto head = juce::jmax<int64_t>(0, first - margin);
            const auto tail = juce::jmin<int64_t>(frames(), last + 1 + margin);
            if (head <= 0 && tail >= frames()) return false;
            for (auto* data : { &left, &right })
            {
                if (data->empty()) continue;
                data->erase(data->begin() + tail, data->end());
                data->erase(data->begin(), data->begin() + head);
            }
            break;
        }
        default:
            return false; // operations added by the next tasks
    }
    recordState();
    return true;
}

bool AudioEditBuffer::undo()
{
    if (! canUndo()) return false;
    --historyIndex;
    restore(history[historyIndex]);
    return true;
}

bool AudioEditBuffer::redo()
{
    if (! canRedo()) return false;
    ++historyIndex;
    restore(history[historyIndex]);
    return true;
}

void AudioEditBuffer::clearHistory()
{
    history.clear();
    history.push_back(currentState());
    historyIndex = 0;
}

size_t AudioEditBuffer::historyBytes() const
{
    size_t bytes = 0;
    for (const auto& state : history)
        bytes += (state.left.size() + state.right.size()) * sizeof(float);
    return bytes;
}

AudioEditBuffer::State AudioEditBuffer::currentState() const
{
    return { left, right, markerData };
}

void AudioEditBuffer::restore(const State& state)
{
    left = state.left;
    right = state.right;
    markerData = state.markerData;
}

void AudioEditBuffer::recordState()
{
    history.resize(historyIndex + 1);
    history.push_back(currentState());
    historyIndex = history.size() - 1;
    // The oldest snapshot is the cheapest to lose: the newest edits stay undoable.
    while (history.size() > 1 && historyBytes() > maxHistoryBytes)
    {
        history.erase(history.begin());
        --historyIndex;
    }
}
} // namespace sonicator::edit
