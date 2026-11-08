#include "Rex2SlicePlayer.h"
#include <algorithm>
#include <cmath>
namespace sonicator {
namespace {
float at(const std::vector<float>& values, int64_t index)
{
    if (index < 0 || index >= (int64_t) values.size()) return 0;
    return values[(size_t) index];
}
float interpolate(const std::vector<float>& values, double position)
{
    const auto index = (int64_t) position;
    const float fraction = (float) (position - (double) index);
    const float a = at(values, index), b = at(values, index + 1);
    return a + (b - a) * fraction;
}
}
std::vector<Rex2Slice> playableRex2Slices(const std::vector<Rex2Slice>& raw, int64_t frames)
{
    std::vector<Rex2Slice> out;
    auto ordered = raw;
    std::stable_sort(ordered.begin(), ordered.end(), [](const Rex2Slice& a, const Rex2Slice& b)
    { return a.start < b.start; });
    for (size_t i = 0; i < ordered.size(); ++i)
    {
        auto slice = ordered[i];
        if ((slice.flags & 1) || slice.start < 0 || slice.start >= frames) continue;
        if (slice.length <= 1)
        {
            int64_t end = frames;
            for (size_t j = i + 1; j < ordered.size(); ++j)
                if (ordered[j].start > slice.start) { end = ordered[j].start; break; }
            slice.length = juce::jmin(end, frames) - slice.start;
        }
        if (slice.length > 1 && slice.length <= frames - slice.start) out.push_back(slice);
    }
    return out;
}
bool Rex2SlicePlayer::load(const juce::File& file, int startNote, juce::String& error)
{
    reset();
    slices.clear(); audioData.reset(); dropped = 0; lastNote = -1;
    firstNote = juce::jlimit(0, 127, startNote);
    Rex2Audio audio;
    if (! decodeRex2Audio(file, audio, error)) return false;
    const auto metadata = extractRex2Metadata(file);
    sourceRate = audio.sampleRate;
    const auto frames = audio.frames;
    audioData = std::make_shared<Rex2Audio>(std::move(audio));
    peak = 0;
    for (float value : audioData->left) peak = juce::jmax(peak, std::abs(value));
    for (const auto& slice : playableRex2Slices(metadata.info.slices, frames))
    {
        if ((slice.flags & 1) != 0 || slice.length <= 1) continue;
        if (slice.start < 0 || slice.start + slice.length > frames) continue;
        if ((int) slices.size() >= maxMappedSlices) { ++dropped; continue; }
        slices.push_back({ slice.start, slice.length });
    }
    if (slices.empty()) { error = "REX2 has no playable slices"; return false; }
    return true;
}
void Rex2SlicePlayer::prepare(double sampleRate)
{
    outputRate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 44100.0;
    reset();
}
void Rex2SlicePlayer::reset()
{
    for (auto& voice : voices) voice = {};
    auditionOn = false; auditionPaused = false; auditionReset = true; auditionFrame = 0;
    auditionPosition = 0;
    for (auto& mask : activeMask) mask = 0;
}
void Rex2SlicePlayer::startAudition(bool loop)
{
    auditionLoop = loop;
    if (! auditionPaused.exchange(false)) auditionReset = true;
    auditionOn = true;
}
void Rex2SlicePlayer::pauseAudition()
{
    auditionOn = false;
    auditionPaused = true;
}
void Rex2SlicePlayer::stopAudition()
{
    auditionOn = false; auditionPaused = false; auditionReset = true;
}
void Rex2SlicePlayer::seekAudition(double seconds)
{
    if (! audioData || sourceRate <= 0) return;
    auditionReset = false;
    auditionFrame = juce::jlimit(0.0, (double) audioData->left.size(), seconds * sourceRate);
    auditionPosition = auditionFrame / sourceRate;
}
double Rex2SlicePlayer::lengthSeconds() const
{
    return audioData && sourceRate > 0 ? (double) audioData->left.size() / sourceRate : 0.0;
}
int Rex2SlicePlayer::activeVoices() const
{
    int count = 0;
    for (const auto& voice : voices) if (voice.active) ++count;
    return count;
}
void Rex2SlicePlayer::release(Voice& voice)
{
    if (voice.releasing) return;
    voice.releasing = true;
    const int frames = juce::jmax(1, (int) std::lround(outputRate * 0.020));
    voice.releaseStep = voice.envelope / (float) frames;
}
void Rex2SlicePlayer::noteOff(int note)
{
    for (auto& voice : voices)
        if (voice.active && voice.gated && voice.note == note) release(voice);
}
void Rex2SlicePlayer::noteOn(int note, float velocity)
{
    lastNote = note;
    lastVel = velocity;
    if (slices.empty() || velocity <= 0) return;
    int index = note - firstNote;
    // Notes outside the mapped range still audition a slice: wrap deterministically. The preview
    // keeps showing the canonical range, so the mapping stays explainable.
    if (index < 0 || index >= (int) slices.size())
        index = ((index % (int) slices.size()) + (int) slices.size()) % (int) slices.size();
    Voice* chosen = nullptr;
    for (auto& voice : voices) if (! voice.active) { chosen = &voice; break; }
    if (chosen == nullptr) return;
    auto& voice = *chosen;
    voice = {};
    voice.active = true;
    voice.note = note;
    voice.slice = index;
    voice.gated = gate;
    voice.velocity = velocity;
    const auto& slice = slices[(size_t) index];
    voice.position = slice.start;
    voice.end = slice.start + slice.length;
    voice.ratio = (double) sourceRate / outputRate;
}
void Rex2SlicePlayer::renderRange(juce::AudioBuffer<float>& out, int start, int count, float midiGain)
{
    if (! audioData) return;
    const auto& left = audioData->left;
    const auto& right = audioData->right;
    const int attackFrames = juce::jmax(1, (int) std::lround(outputRate * 0.002));
    const int fadeFrames = juce::jmax(1, (int) std::lround(outputRate * 0.005));
    if (auditionReset.exchange(false)) auditionFrame = 0;
    const bool on = auditionOn.load();
    if (on && ! audioData->left.empty())
    {
        const double ratio = (double) sourceRate / outputRate;
        const double end = (double) audioData->left.size();
        for (int i = 0; i < count; ++i)
        {
            if (auditionFrame >= end)
            {
                if (auditionLoop.load()) auditionFrame = std::fmod(auditionFrame, end);
                else { auditionOn = false; break; }
            }
            const float l = interpolate(left, auditionFrame);
            const float r = right.empty() ? l : interpolate(right, auditionFrame);
            const float gain = 0.25f;
            if (out.getNumChannels() > 0) out.addSample(0, start + i, l * gain);
            if (out.getNumChannels() > 1) out.addSample(1, start + i, r * gain);
            auditionFrame += ratio;
        }
        auditionPosition = auditionFrame / sourceRate;
    }
    for (auto& voice : voices)
    {
        if (! voice.active) continue;
        for (int i = 0; i < count && voice.active; ++i)
        {
            if (voice.position >= (double) voice.end) { voice.active = false; break; }
            const double remaining = (double) voice.end - voice.position;
            const float tail = remaining < fadeFrames ? (float) (remaining / fadeFrames) : 1.0f;
            const float l = interpolate(left, voice.position);
            const float r = right.empty() ? l : interpolate(right, voice.position);
            const float gain = voice.envelope * voice.velocity * tail * 0.25f * midiGain;
            if (out.getNumChannels() > 0) out.addSample(0, start + i, l * gain);
            if (out.getNumChannels() > 1) out.addSample(1, start + i, r * gain);
            voice.position += voice.ratio;
            if (voice.releasing)
            {
                voice.envelope = juce::jmax(0.0f, voice.envelope - voice.releaseStep);
                if (voice.envelope <= 0) voice.active = false;
            }
            else voice.envelope = juce::jmin(1.0f, voice.envelope + 1.0f / (float) attackFrames);
        }
    }
}
void Rex2SlicePlayer::render(juce::AudioBuffer<float>& out, int start, int count,
                             const juce::MidiBuffer& midi, float midiGain)
{
    juce::ScopedNoDenormals noDenormals;
    out.clear(start, count);
    int position = 0;
    for (const auto metadata : midi)
    {
        const int frame = juce::jlimit(position, count, metadata.samplePosition);
        renderRange(out, start + position, frame - position, midiGain);
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
    renderRange(out, start + position, count - position, midiGain);
    juce::uint64 low = 0, high = 0;
    for (const auto& voice : voices)
        if (voice.active && voice.slice >= 0)
        {
            if (voice.slice < 64) low |= (juce::uint64) 1 << voice.slice;
            else if (voice.slice < 128) high |= (juce::uint64) 1 << (voice.slice - 64);
        }
    activeMask[0] = low; activeMask[1] = high;
    mixPeak = out.getMagnitude(0, start, count);
    firstEnv = 0; firstPos = 0;
    for (const auto& voice : voices)
        if (voice.active) { firstEnv = voice.envelope; firstPos = voice.position; break; }
}
std::vector<int> Rex2SlicePlayer::activeSlices() const
{
    std::vector<int> indices;
    const auto low = activeMask[0].load(), high = activeMask[1].load();
    for (int i = 0; i < 128; ++i)
    {
        const auto mask = i < 64 ? low : high;
        if ((mask & ((juce::uint64) 1 << (i < 64 ? i : i - 64))) != 0) indices.push_back(i);
    }
    return indices;
}
std::vector<int64_t> Rex2SlicePlayer::sliceStarts() const
{
    std::vector<int64_t> starts;
    for (const auto& slice : slices) starts.push_back(slice.start);
    return starts;
}
} // namespace sonicator
