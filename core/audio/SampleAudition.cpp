#include "SampleAudition.h"
#include <cmath>
namespace sonicator {
void SampleAudition::setAudio(SampleAudioPtr audio)
{
    playing = false;
    positionFrames = 0;
    looping = false;
    source = std::move(audio);
}
void SampleAudition::prepare(double rate)
{
    outputRate = std::isfinite(rate) && rate > 0 ? rate : 44100.0;
    stop();
}
void SampleAudition::play(bool wantLoop)
{
    if (! hasAudio()) return;
    if (positionFrames.load() >= source->frames) positionFrames = 0;
    looping = wantLoop;
    playing = true;
}
void SampleAudition::pause() { playing = false; }
void SampleAudition::stop() { playing = false; positionFrames = 0; }
void SampleAudition::seek(double seconds)
{
    if (! hasAudio() || ! std::isfinite(seconds)) return;
    const auto frame = (int64_t) std::llround(seconds * (double) source->sampleRate);
    positionFrames = juce::jlimit<int64_t>(0, source->frames, frame);
}
double SampleAudition::position() const
{
    if (! hasAudio() || source->sampleRate <= 0) return 0;
    return (double) positionFrames.load() / (double) source->sampleRate;
}
double SampleAudition::length() const
{
    if (! hasAudio() || source->sampleRate <= 0) return 0;
    return (double) source->frames / (double) source->sampleRate;
}
void SampleAudition::render(juce::AudioBuffer<float>& out, int start, int count)
{
    if (! playing.load(std::memory_order_relaxed)) return;
    const auto* audio = source.get();
    if (audio == nullptr || audio->frames <= 0 || audio->sampleRate <= 0) return;
    const auto frames = audio->frames;
    const auto increment = (double) audio->sampleRate / outputRate;
    auto position = (double) positionFrames.load(std::memory_order_relaxed);
    int produced = 0;
    for (; produced < count; ++produced)
    {
        if (position >= (double) frames)
        {
            if (! looping.load(std::memory_order_relaxed))
            {
                playing = false;
                break;
            }
            position = std::fmod(position, (double) frames);
        }
        const auto index = (int64_t) position;
        const auto fraction = (float) (position - (double) index);
        const auto next = juce::jmin(frames - 1, index + 1);
        const auto read = [index, next, fraction] (const std::vector<float>& channel)
        {
            const float a = channel[(size_t) index];
            const float b = channel[(size_t) next];
            return a + (b - a) * fraction;
        };
        const float left = read(audio->left);
        const float right = audio->right.empty() ? left : read(audio->right);
        if (out.getNumChannels() > 0) out.addSample(0, start + produced, left);
        if (out.getNumChannels() > 1) out.addSample(1, start + produced, right);
        position += increment;
    }
    positionFrames = (int64_t) position;
}
} // namespace sonicator
