#include "SampleVoicePlayer.h"
#include <cmath>
namespace sonicator {
void SampleVoicePlayer::setSource(SampleAudioPtr audio, bool loopAvailable, int64_t loopStart,
                                  int64_t loopEnd, bool pingPong)
{
    int slot = -1, idle = -1;
    for (int i = 0; i < maxSources; ++i)
    {
        if (sources[(size_t) i].audio == nullptr) { slot = i; break; }
        if (sources[(size_t) i].users == 0 && idle < 0) idle = i;
    }
    if (slot < 0) slot = idle;
    if (slot < 0)
    {
        // Every retained source is still ringing. Stop the oldest voices rather than let a
        // shared source table grow without bound.
        reset();
        slot = 0;
    }
    auto& source = sources[(size_t) slot];
    source.audio = std::move(audio);
    source.loopAvailable = loopAvailable && loopStart >= 0 && loopEnd > loopStart + 1;
    source.loopStart = loopStart;
    source.loopEnd = loopEnd;
    source.pingPong = pingPong;
    currentSource = slot;
}

void SampleVoicePlayer::clear()
{
    reset();
    for (auto& source : sources) source = {};
    currentSource = -1;
}

void SampleVoicePlayer::prepare(double sampleRate)
{
    outputRate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 44100.0;
    reset();
}

void SampleVoicePlayer::reset()
{
    for (auto& voice : voices)
    {
        if (voice.active && voice.source >= 0 && voice.source < maxSources)
            sources[(size_t) voice.source].users = juce::jmax(0, sources[(size_t) voice.source].users - 1);
        voice = {};
    }
    sustain.fill(false);
}

int SampleVoicePlayer::activeVoices() const
{
    int count = 0;
    for (const auto& voice : voices) if (voice.active) ++count;
    return count;
}

void SampleVoicePlayer::release(Voice& voice)
{
    if (voice.releasing) return;
    voice.sustained = false;
    voice.releasing = true;
    const float frames = (float) juce::jmax(1.0, outputRate * 2.0); // 2 s release
    voice.releaseStep = voice.envelope / frames;
}

void SampleVoicePlayer::stopVoice(Voice& voice)
{
    if (voice.active && voice.source >= 0 && voice.source < maxSources)
        sources[(size_t) voice.source].users = juce::jmax(0, sources[(size_t) voice.source].users - 1);
    voice = {};
}

void SampleVoicePlayer::event(const juce::MidiMessage& message)
{
    const int channel = message.getChannel();
    if (channel < 1 || channel > 16) return;
    if (message.isAllSoundOff())
    {
        for (auto& voice : voices) if (voice.channel == channel) stopVoice(voice);
        return;
    }
    if (message.isAllNotesOff())
    {
        for (auto& voice : voices) if (voice.channel == channel) release(voice);
        return;
    }
    if (message.isController() && message.getControllerNumber() == 64)
    {
        const bool down = message.getControllerValue() >= 64;
        sustain[(size_t) channel - 1] = down;
        if (! down)
            for (auto& voice : voices)
                if (voice.channel == channel && voice.sustained) release(voice);
        return;
    }
    if (message.isNoteOff())
    {
        for (auto& voice : voices)
            if (voice.active && voice.note == message.getNoteNumber() && voice.channel == channel)
            {
                if (sustain[(size_t) channel - 1]) voice.sustained = true;
                else release(voice);
            }
        return;
    }
    if (! message.isNoteOn() || currentSource < 0) return;
    const auto& source = sources[(size_t) currentSource];
    if (source.audio == nullptr || source.audio->frames <= 0 || source.audio->sampleRate <= 0) return;
    Voice* chosen = nullptr;
    for (auto& voice : voices) if (! voice.active) { chosen = &voice; break; }
    if (chosen == nullptr) return; // never hard-steal an audible voice
    auto& voice = *chosen;
    voice = {};
    voice.active = true;
    voice.note = message.getNoteNumber();
    voice.channel = channel;
    voice.velocity = message.getFloatVelocity();
    voice.source = currentSource;
    voice.position = 0;
    voice.direction = 1;
    voice.loop = looping.load(std::memory_order_relaxed) && source.loopAvailable;
    const double semitones = (double) (voice.note - 60) / 12.0;
    voice.increment = juce::jlimit(0.0001, 64.0,
        (double) source.audio->sampleRate / outputRate * std::pow(2.0, semitones));
    voice.increment = std::isfinite(voice.increment) ? voice.increment : 1.0;
    voice.releaseStep = 0;
    sources[(size_t) currentSource].users++;
}

void SampleVoicePlayer::renderRange(juce::AudioBuffer<float>& out, int start, int count)
{
    for (auto& voice : voices)
    {
        if (! voice.active || voice.source < 0 || voice.source >= maxSources) continue;
        const auto& source = sources[(size_t) voice.source];
        const auto* audio = source.audio.get();
        if (audio == nullptr || audio->frames <= 0) { stopVoice(voice); continue; }
        const auto frames = audio->frames;
        for (int i = 0; i < count && voice.active; ++i)
        {
            if (voice.loop)
            {
                const auto loopStart = (double) source.loopStart;
                const auto loopEnd = (double) source.loopEnd;
                if (voice.position >= loopEnd)
                {
                    if (source.pingPong)
                    {
                        voice.position = loopEnd - (voice.position - loopEnd) - 1.0;
                        voice.direction = -1;
                    }
                    else voice.position = loopStart + std::fmod(voice.position - loopStart, loopEnd - loopStart);
                }
                else if (voice.direction < 0 && voice.position < loopStart)
                {
                    voice.position = loopStart + (loopStart - voice.position);
                    voice.direction = 1;
                }
            }
            else if (voice.position >= (double) frames)
            {
                stopVoice(voice);
                break;
            }
            const auto index = juce::jlimit<int64_t>(0, frames - 1, (int64_t) voice.position);
            const auto fraction = (float) (voice.position - (double) index);
            const auto next = voice.direction < 0 ? juce::jmax<int64_t>(0, index - 1)
                                                  : juce::jmin<int64_t>(frames - 1, index + 1);
            const auto read = [index, next, fraction] (const std::vector<float>& channel)
            {
                const float a = channel[(size_t) index];
                const float b = channel[(size_t) next];
                return a + (b - a) * fraction;
            };
            const float left = read(audio->left);
            const float right = audio->right.empty() ? left : read(audio->right);
            const float gain = voice.envelope * voice.velocity * 0.25f;
            if (out.getNumChannels() > 0) out.addSample(0, start + i, left * gain);
            if (out.getNumChannels() > 1) out.addSample(1, start + i, right * gain);
            voice.position += voice.increment * (double) voice.direction;
            if (voice.releasing)
            {
                voice.envelope = juce::jmax(0.0f, voice.envelope - voice.releaseStep);
                if (voice.envelope <= 0.0f) { stopVoice(voice); break; }
            }
            else voice.envelope = juce::jmin(1.0f, voice.envelope + 1.0f / (float) juce::jmax(1.0, outputRate * 0.002));
        }
    }
}

void SampleVoicePlayer::render(juce::AudioBuffer<float>& out, int start, int count,
                               const juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    out.clear(start, count);
    int position = 0;
    for (const auto metadata : midi)
    {
        const int frame = juce::jlimit(position, count, metadata.samplePosition);
        renderRange(out, start + position, frame - position);
        position = frame;
        if (metadata.numBytes <= 3) event(metadata.getMessage());
    }
    renderRange(out, start + position, count - position);
}
} // namespace sonicator
