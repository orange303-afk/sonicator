#include "ExternalKitSampler.h"
#include <cmath>
namespace sonicator {
bool ExternalKitSampler::load(const FileInstrument& instrument, juce::String& error)
{
    reset();
    telemetry.reset();
    if (! instrument.isPlayable())
    {
        error = "Instrument has no playable zones";
        return false;
    }
    zones = instrument.zones;
    sources = instrument.audio;
    error.clear();
    return true;
}
void ExternalKitSampler::prepare(double sampleRate)
{
    outputRate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 44100.0;
    reset();
}
void ExternalKitSampler::reset()
{
    for (auto& voice : voices) voice = {};
}
int ExternalKitSampler::activeVoices() const
{
    int count = 0;
    for (const auto& voice : voices) if (voice.active) ++count;
    return count;
}
void ExternalKitSampler::release(Voice& voice)
{
    if (voice.releasing) return;
    voice.releasing = true;
    const int frames = juce::jmax(1, (int) std::lround(outputRate * 2.0));
    voice.releaseStep = voice.envelope / (float) frames;
}
void ExternalKitSampler::noteOff(int note)
{
    for (auto& voice : voices)
        if (voice.active && ! voice.releasing && ! voice.oneShot && voice.note == note) release(voice);
}
void ExternalKitSampler::noteOn(int note, float velocity)
{
    if (velocity <= 0) return;
    lastNote.store(note, std::memory_order_relaxed);
    const int midiVelocity = juce::jlimit(1, 127, (int) std::lround(velocity * 127.0f));
    SampleId firstSource = invalidSampleId;
    int layers = 0;
    for (const auto& zone : zones)
    {
        if (zone.sampleIndex < 0 || zone.sampleIndex >= (int) sources.size()) continue;
        const auto& audio = sources[(size_t) zone.sampleIndex];
        if (audio == nullptr || audio->frames <= 0) continue;
        if (note < zone.keyLow || note > zone.keyHigh) continue;
        if (midiVelocity < zone.velocityLow || midiVelocity > zone.velocityHigh) continue;
        Voice* chosen = nullptr;
        for (auto& voice : voices) if (! voice.active) { chosen = &voice; break; }
        if (chosen == nullptr) break;
        auto& voice = *chosen;
        voice = {};
        voice.active = true;
        voice.note = note;
        voice.source = zone.sampleIndex;
        voice.position = zone.start;
        voice.end = zone.end;
        voice.loopStart = zone.loopStart;
        voice.loopEnd = zone.loopEnd;
        voice.looping = zone.looping && zone.loopEnd > zone.loopStart;
        voice.oneShot = zone.oneShot;
        voice.increment = (audio->sampleRate / outputRate)
                        * std::pow(2.0, ((double) note - (double) zone.rootKey
                                         + zone.tuningCents / 100.0) / 12.0);
        voice.velocity = velocity * std::pow(10.0, -zone.attenuationDb / 20.0);
        if (firstSource == invalidSampleId) firstSource = zone.sampleIndex;
        ++layers;
    }
    lastLayers.store(layers, std::memory_order_relaxed);
    if (firstSource != invalidSampleId) telemetry.publish(generation, firstSource, layers);
}
void ExternalKitSampler::renderRange(juce::AudioBuffer<float>& out, int start, int count)
{
    const int attackFrames = juce::jmax(1, (int) std::lround(outputRate * 0.002));
    for (auto& voice : voices)
    {
        if (! voice.active) continue;
        const auto audio = sources[(size_t) voice.source];
        if (audio == nullptr) { voice.active = false; continue; }
        for (int i = 0; i < count && voice.active; ++i)
        {
            if (voice.position >= voice.end)
            {
                if (! voice.looping) { voice.active = false; break; }
                const auto span = voice.loopEnd - voice.loopStart;
                if (span <= 0) { voice.active = false; break; }
                voice.position = voice.loopStart + std::fmod((double) (voice.position - voice.loopStart),
                                                            (double) span);
            }
            if (voice.looping && voice.position >= voice.loopEnd)
            {
                const auto span = voice.loopEnd - voice.loopStart;
                voice.position = voice.loopStart + std::fmod((double) (voice.position - voice.loopStart),
                                                            (double) span);
            }
            const auto index = (int64_t) voice.position;
            const auto fraction = (float) (voice.position - (double) index);
            const auto next = juce::jmin(audio->frames - 1, index + 1);
            const auto read = [index, next, fraction] (const std::vector<float>& channel)
            {
                if ((size_t) index >= channel.size()) return 0.0f;
                const float a = channel[(size_t) index];
                const float b = channel[(size_t) next];
                return a + (b - a) * fraction;
            };
            const float left = read(audio->left);
            const float right = audio->right.empty() ? left : read(audio->right);
            const float gain = voice.envelope * voice.velocity * 0.25f;
            if (out.getNumChannels() > 0) out.addSample(0, start + i, left * gain);
            if (out.getNumChannels() > 1) out.addSample(1, start + i, right * gain);
            voice.position += voice.increment;
            if (voice.releasing)
            {
                voice.envelope = juce::jmax(0.0f, voice.envelope - voice.releaseStep);
                if (voice.envelope <= 0) voice.active = false;
            }
            else voice.envelope = juce::jmin(1.0f, voice.envelope + 1.0f / (float) attackFrames);
        }
    }
}
void ExternalKitSampler::render(juce::AudioBuffer<float>& out, int start, int count,
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
        if (metadata.numBytes <= 3)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) noteOn(message.getNoteNumber(), message.getFloatVelocity());
            else if (message.isNoteOff()) noteOff(message.getNoteNumber());
            else if (message.isAllNotesOff())
            {
                for (auto& voice : voices) if (voice.active) release(voice);
            }
            else if (message.isAllSoundOff()) for (auto& voice : voices) voice.active = false;
        }
    }
    renderRange(out, start + position, count - position);
}
} // namespace sonicator
