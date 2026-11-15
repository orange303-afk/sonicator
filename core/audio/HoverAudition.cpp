#include "HoverAudition.h"
#include "core/dsp/AnalysisAudio.h"
#include <cmath>

namespace sonicator
{
namespace
{
constexpr double fadeSeconds = 0.010;
}

void HoverAudition::setAudio(ClipPtr audio)
{
    playing = false;
    fadingOut = false;
    gain = 0.0f; // the next play ramps up from silence, so swapping clips cannot click
    positionFrames = 0;
    clip = std::move(audio);
}

void HoverAudition::clear()
{
    setAudio({});
}

void HoverAudition::prepare(double rate)
{
    outputRate = std::isfinite(rate) && rate > 0 ? rate : 44100.0;
}

void HoverAudition::play()
{
    if (! hasAudio()) return;
    positionFrames = 0;
    fadingOut = false;
    playing = true;
}

void HoverAudition::stop()
{
    if (! playing.load(std::memory_order_relaxed)) return;
    fadingOut = true;
}

double HoverAudition::position() const
{
    if (! hasAudio() || clip->sampleRate <= 0) return 0;
    return positionFrames.load(std::memory_order_relaxed) / clip->sampleRate;
}

double HoverAudition::length() const
{
    if (! hasAudio() || clip->sampleRate <= 0) return 0;
    return (double) clip->frames / clip->sampleRate;
}

void HoverAudition::render(juce::AudioBuffer<float>& out, int start, int count)
{
    if (! playing.load(std::memory_order_relaxed)) return;
    const auto active = clip;
    if (active == nullptr || active->frames <= 0 || active->sampleRate <= 0) return;
    const auto increment = active->sampleRate / outputRate;
    const auto step = (float) (1.0 / (fadeSeconds * outputRate));
    const bool outGoing = fadingOut.load(std::memory_order_relaxed);
    auto position = positionFrames.load(std::memory_order_relaxed);
    auto level = gain.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
    {
        if (position >= (double) active->frames)
        {
            playing = false;
            break;
        }
        const auto index = (int64_t) position;
        const auto fraction = (float) (position - (double) index);
        const auto next = juce::jmin<int64_t>(active->frames - 1, index + 1);
        auto read = [index, next, fraction](const juce::AudioBuffer<float>& buffer, int channel)
        {
            const auto* data = buffer.getReadPointer(channel);
            return data[index] + (data[next] - data[index]) * fraction;
        };
        const auto left = read(active->buffer, 0);
        const auto right = active->buffer.getNumChannels() > 1 ? read(active->buffer, 1) : left;
        const auto target = outGoing ? 0.0f : 1.0f;
        level = target > level ? juce::jmin(target, level + step) : juce::jmax(target, level - step);
        if (outGoing && level <= 0.0f)
        {
            gain = 0.0f;
            playing = false;
            break;
        }
        if (out.getNumChannels() > 0) out.addSample(0, start + i, left * level);
        if (out.getNumChannels() > 1) out.addSample(1, start + i, right * level);
        position += increment;
    }
    gain = level;
    positionFrames = position;
}

HoverAudition::ClipPtr HoverAudition::load(const juce::File& file, double maxSeconds)
{
    AnalysisAudioOptions options;
    options.maxSeconds = maxSeconds;
    auto audio = readAnalysisAudio(file, options);
    if (! audio.ok()) return {};
    auto clip = std::make_shared<Clip>();
    clip->buffer = std::move(audio.buffer);
    clip->sampleRate = audio.sampleRate;
    clip->frames = clip->buffer.getNumSamples();
    if (clip->frames <= 0) return {};
    return clip;
}
}
