#include "SampleAudition.h"
#include <cmath>
namespace sonicator {
void SampleAudition::setAudio(SampleAudioPtr audio)
{
    playing = false;
    positionFrames = 0;
    looping = false;
    direction.store(1, std::memory_order_relaxed);
    source = std::move(audio);
    loopStart = 0;
    loopEnd = source != nullptr ? source->frames : 0;
    pingPong = false;
}

void SampleAudition::setLoopRange(int64_t start, int64_t end, bool wantPingPong)
{
    const auto frames = source != nullptr ? source->frames : 0;
    if (frames <= 0 || start < 0 || end <= start + 1 || end > frames)
    {
        loopStart = 0;
        loopEnd = frames;
        pingPong = false;
        return;
    }
    loopStart = start;
    loopEnd = end;
    pingPong = wantPingPong;
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
    direction.store(1, std::memory_order_relaxed);
    playing = true;
}

void SampleAudition::pause() { playing = false; }

void SampleAudition::stop() { playing = false; positionFrames = 0; direction.store(1, std::memory_order_relaxed); }

void SampleAudition::seek(double seconds)
{
    if (! hasAudio() || ! std::isfinite(seconds)) return;
    const auto frame = (int64_t) std::llround(seconds * (double) source->sampleRate);
    positionFrames = juce::jlimit<int64_t>(0, source->frames, frame);
    direction.store(1, std::memory_order_relaxed);
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
    const bool isLooping = looping.load(std::memory_order_relaxed);
    const auto begin = (double) (isLooping ? loopStart : 0);
    const auto finish = (double) (isLooping ? loopEnd : frames);
    if (finish <= begin) return;
    auto position = (double) positionFrames.load(std::memory_order_relaxed);
    int produced = 0;
    for (; produced < count; ++produced)
    {
        if (position >= finish)
        {
            if (! isLooping)
            {
                playing = false;
                break;
            }
            if (pingPong)
            {
                position = finish - (position - finish) - 1.0;
                direction.store(-1, std::memory_order_relaxed);
            }
            else position = begin + std::fmod(position - begin, finish - begin);
        }
        else if (direction.load(std::memory_order_relaxed) < 0 && position < begin)
        {
            // Only a ping-pong overshoot reflects here. The initial pre-loop playback starts at
            // frame 0 and must be allowed to reach loopEnd before any looping begins.
            position = begin + (begin - position);
            direction.store(1, std::memory_order_relaxed);
        }
        const auto index = juce::jlimit<int64_t>(0, frames - 1, (int64_t) position);
        const auto fraction = (float) (position - (double) index);
        const auto next = direction.load(std::memory_order_relaxed) < 0
            ? juce::jmax<int64_t>(0, index - 1) : juce::jmin(frames - 1, index + 1);
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
        position += increment * (double) direction.load(std::memory_order_relaxed);
    }
    positionFrames = (int64_t) position;
}
} // namespace sonicator
