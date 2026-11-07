#pragma once
#include "Rex2Decoder.h"
#include "core/smart/Rex2Metadata.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include <atomic>
namespace sonicator {
// Promote marker-only entries to frame ranges before assigning MIDI notes.
std::vector<Rex2Slice> playableRex2Slices(const std::vector<Rex2Slice>&, int64_t frames);
// Plays the visible slices of a decoded REX2 waveform as a MIDI drum map: the first slice is
// startNote, each following slice the next note. Gate mode stops a slice on NoteOff; one-shot
// mode ignores NoteOff. Loading is message-thread only; render() allocates nothing.
class Rex2SlicePlayer
{
public:
    static constexpr int maxVoices = 16;
    static constexpr int maxMappedSlices = 128;
    bool load(const juce::File&, int startNote, juce::String& error);
    void prepare(double sampleRate);
    void reset();
    void setGateMode(bool enabled) { gate = enabled; }
    bool gateMode() const { return gate; }
    int sliceCount() const { return (int) slices.size(); }
    int startNote() const { return firstNote; }
    int droppedSlices() const { return dropped; }
    std::shared_ptr<const Rex2Audio> audio() const { return audioData; }
    int lastMidiNote() const { return lastNote.load(); }
    float decodedPeak() const { return peak; }
    float lastVelocity() const { return lastVel.load(); }
    float lastMixPeak() const { return mixPeak.load(); }
    float firstVoiceEnvelope() const { return firstEnv.load(); }
    double firstVoicePosition() const { return firstPos.load(); }
    std::vector<int> activeSlices() const;
    // Whole-loop audition used by the ordinary Play/Stop transport.
    void startAudition(bool loop);
    void pauseAudition();
    void stopAudition();
    void seekAudition(double seconds);
    void setAuditionLoop(bool loop) { auditionLoop = loop; }
    bool auditionPlaying() const { return auditionOn.load(); }
    double auditionSeconds() const { return auditionPosition.load(); }
    double lengthSeconds() const;
    std::vector<int64_t> sliceStarts() const;
    void render(juce::AudioBuffer<float>&, int start, int count, const juce::MidiBuffer&);
    int activeVoices() const;
private:
    struct Voice
    {
        bool active = false, releasing = false, gated = false;
        int note = 0, slice = -1;
        double position = 0;
        int64_t end = 0;
        double ratio = 1;
        float envelope = 0, releaseStep = 0, velocity = 0;
    };
    struct Slice { int64_t start = 0, length = 0; };
    std::array<Voice, maxVoices> voices;
    std::vector<Slice> slices;
    std::shared_ptr<const Rex2Audio> audioData;
    int sourceRate = 44100;
    int firstNote = 36;
    int dropped = 0;
    float peak = 0;
    double outputRate = 44100;
    bool gate = true;
    std::atomic<int> lastNote { -1 };
    std::atomic<float> lastVel { 0 };
    std::atomic<float> mixPeak { 0 }, firstEnv { 0 };
    std::atomic<double> firstPos { 0 };
    std::array<std::atomic<juce::uint64>, 2> activeMask {};
    std::atomic<bool> auditionOn { false }, auditionLoop { false }, auditionReset { false }, auditionPaused { false };
    std::atomic<double> auditionPosition { 0 };
    double auditionFrame = 0;
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void release(Voice&);
    void renderRange(juce::AudioBuffer<float>&, int start, int count);
};
} // namespace sonicator
