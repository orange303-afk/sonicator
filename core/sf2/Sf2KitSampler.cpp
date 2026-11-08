#include "Sf2KitSampler.h"
#include <cmath>
namespace sonicator {
namespace {
float sampleAt(const std::vector<float>& pcm, int index)
{
    if (index < 0 || index >= (int) pcm.size()) return 0;
    return pcm[(size_t) index];
}
float interpolate(const std::vector<float>& pcm, double position)
{
    const auto index = (int) position;
    const float fraction = (float) (position - index);
    const float a = sampleAt(pcm, index), b = sampleAt(pcm, index + 1);
    return a + (b - a) * fraction;
}
}
bool Sf2KitSampler::load(const Sf2Bank& bank, int presetIndex, juce::String& error)
{
    reset();
    telemetry.reset();
    zones.clear(); samples.clear(); pcm.clear(); preset = -1; name.clear();
    if (! bank.isLoaded()) { error = "SoundFont is not loaded"; return false; }
    if (presetIndex < 0 || presetIndex >= (int) bank.presets().size())
    {
        error = "SoundFont preset index is out of range";
        return false;
    }
    const auto& source = bank.presets()[(size_t) presetIndex];
    if (source.zones.empty()) { error = "SoundFont preset has no zones"; return false; }
    zones = source.zones;
    samples = bank.samples();
    pcm.reserve(bank.pcm().size());
    for (const auto value : bank.pcm()) pcm.push_back(value / 32768.0f);
    preset = presetIndex;
    name = source.name;
    for (auto& zone : zones)
    {
        if (zone.sampleIndex < 0 || zone.sampleIndex >= (int) samples.size())
        {
            zone.sampleIndex = -1;
            continue;
        }
        const auto& sample = samples[(size_t) zone.sampleIndex];
        if (sample.sampleRate <= 0 || sample.start < 0 || sample.end <= sample.start
            || sample.end > (int64_t) pcm.size())
            zone.sampleIndex = -1;
    }
    return true;
}
void Sf2KitSampler::prepare(double sampleRate)
{
    outputRate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 44100.0;
    reset();
}
void Sf2KitSampler::reset()
{
    for (auto& voice : voices) voice = {};
}
int Sf2KitSampler::activeVoices() const
{
    int count = 0;
    for (const auto& voice : voices) if (voice.active) ++count;
    return count;
}
void Sf2KitSampler::release(Voice& voice, double seconds)
{
    if (voice.releasing && seconds >= 2.0) return;
    voice.releasing = true;
    const int frames = juce::jmax(1, (int) std::lround(outputRate * seconds));
    voice.releaseStep = juce::jmax(voice.releaseStep, voice.envelope / (float) frames);
}
void Sf2KitSampler::noteOff(int note)
{
    for (auto& voice : voices)
        if (voice.active && ! voice.releasing && voice.note == note) release(voice);
}
void Sf2KitSampler::noteOn(int note, float velocity)
{
    if (velocity <= 0) return;
    lastNote.store(note, std::memory_order_relaxed);
    const int midiVelocity = juce::jlimit(1, 127, (int) std::lround(velocity * 127.0f));
    SampleId firstSource = invalidSampleId;
    int layers = 0;
    for (const auto& zone : zones)
    {
        if (zone.sampleIndex < 0) continue;
        if (note < zone.keyLow || note > zone.keyHigh) continue;
        if (midiVelocity < zone.velocityLow || midiVelocity > zone.velocityHigh) continue;
        Voice* chosen = nullptr;
        for (auto& voice : voices) if (! voice.active) { chosen = &voice; break; }
        if (chosen == nullptr) break; // never steal an audible voice
        // Drums choke their exclusive group. Stop the previous voice immediately but with the
        // short release ramp rather than a hard cut.
        if (zone.exclusiveClass != 0)
            for (auto& voice : voices)
                if (&voice != chosen && voice.active && voice.exclusive == zone.exclusiveClass)
                    release(voice, 0.020);
        auto& voice = *chosen;
        voice = {};
        voice.active = true;
        voice.note = note;
        voice.exclusive = zone.exclusiveClass;
        const auto& sample = samples[(size_t) zone.sampleIndex];
        voice.left = zone.sampleIndex;
        voice.right = sample.sampleLink > 0 && sample.sampleLink < (int) samples.size()
                    ? sample.sampleLink : -1;
        voice.position = (double) sample.start;
        voice.loopStart = juce::jlimit(sample.start, sample.end, sample.loopStart);
        voice.loopEnd = juce::jlimit(sample.start, sample.end, sample.loopEnd);
        voice.looping = zone.loopMode != 0 && voice.loopEnd > voice.loopStart;
        const double tuning = zone.tuningCents / 100.0;
        voice.increment = (sample.sampleRate / outputRate)
                        * std::pow(2.0, (note - zone.rootKey + tuning) / 12.0);
        voice.velocity = velocity * std::pow(10.0, -zone.attenuationDb / 20.0);
        voice.envelope = 0;
        voice.releaseStep = 0;
        voice.position = (double) sample.start;
        if (firstSource == invalidSampleId)
            firstSource = sample.sampleType == 2 && sample.sampleLink < (int) samples.size()
                              && sample.sampleLink != zone.sampleIndex
                        ? sample.sampleLink : zone.sampleIndex;
        ++layers;
    }
    // Publish only after at least one voice really started. A NoteOn without a matching zone or
    // one rejected by the voice limit leaves the previous waveform untouched.
    lastLayers.store(layers, std::memory_order_relaxed);
    if (firstSource != invalidSampleId) telemetry.publish(generation, firstSource, layers);
}
void Sf2KitSampler::renderRange(juce::AudioBuffer<float>& out, int start, int count)
{
    const int attackFrames = juce::jmax(1, (int) std::lround(outputRate * 0.002));
    for (auto& voice : voices)
    {
        if (! voice.active) continue;
        const auto& leftSample = samples[(size_t) voice.left];
        const bool hasRight = voice.right >= 0;
        const auto& rightSample = hasRight ? samples[(size_t) voice.right] : leftSample;
        for (int i = 0; i < count && voice.active; ++i)
        {
            if (voice.position >= (double) leftSample.end)
            {
                if (! voice.looping) { voice.active = false; break; }
                voice.position = (double) voice.loopStart
                    + std::fmod(voice.position - (double) voice.loopStart,
                                (double) (voice.loopEnd - voice.loopStart));
            }
            if (voice.looping && voice.position >= (double) voice.loopEnd)
                voice.position = (double) voice.loopStart
                    + std::fmod(voice.position - (double) voice.loopStart,
                                (double) (voice.loopEnd - voice.loopStart));
            const float left = interpolate(pcm, voice.position);
            const double rightPosition = hasRight
                ? (double) rightSample.start + (voice.position - (double) leftSample.start)
                : voice.position;
            const float right = hasRight ? interpolate(pcm, rightPosition) : left;
            const float gain = voice.envelope * voice.velocity * 0.25f;
            if (out.getNumChannels() > 0) out.addSample(0, start + i, left * gain);
            if (out.getNumChannels() > 1) out.addSample(1, start + i, right * gain);
            voice.position += voice.increment;
            if (voice.releasing)
            {
                voice.envelope = juce::jmax(0.0f, voice.envelope - voice.releaseStep);
                if (voice.envelope <= 0) voice.active = false;
            }
            else
            {
                voice.envelope = juce::jmin(1.0f, voice.envelope + 1.0f / (float) attackFrames);
            }
        }
    }
}
void Sf2KitSampler::render(juce::AudioBuffer<float>& out, int start, int count,
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
