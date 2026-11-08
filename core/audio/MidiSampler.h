#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/sf2/Sf2KitSampler.h"
#include "core/rex/Rex2SlicePlayer.h"
#include "SampleAudition.h"
#include "SampleVoicePlayer.h"
#include "core/sampler/ExternalKitSampler.h"
#include "core/tracker/TrackerSongRenderer.h"
#include <array>
#include <atomic>
namespace sonicator {
int64_t firstAudibleFrame(juce::AudioFormatReader&);
// Bounded multi-producer MIDI queue. SysEx is rejected before storage. Audio never waits:
// a contended drain defers events to the next block; input contention/overflow requests panic.
class MidiEventQueue final : public juce::MidiInputCallback
{
public:
    static constexpr int capacity = 2048;
    void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& m) override { push(m); }
    void push(const juce::MidiMessage&);
    bool drain(juce::MidiBuffer&); // true means overflow panic (release all voices).
private:
    struct Event { std::array<juce::uint8,3> bytes {}; int size = 0; };
    std::array<Event,capacity> events;
    int count = 0;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    std::atomic<bool> panic {false};
};
// Sample publication/load and reset require the output callback to be detached.
// render() and voices are audio-thread only; scalar options are atomic snapshots per note.
class MidiSampler
{
public:
    juce::String load(const juce::File&);
    void clear();
    void prepare(double rate);
    void reset();
    void render(juce::AudioBuffer<float>&,int start,int count,const juce::MidiBuffer&);
    int activeVoices() const;
    int64_t silenceStart() const { return onset; }
    std::atomic<bool> skipSilence {false}, loop {false};
private:
    struct Voice
    {
        bool active = false, releasing = false, looping = false, sustained = false;
        int note = 0, channel = 1, age = 0, attackFrames = 1, releaseFrames = 1, startFrame = 0;
        double position = 0, increment = 1;
        float envelope = 0, releaseStep = 0, velocity = 0;
    };
    std::array<Voice,16> voices;
    std::array<bool,16> sustain {};
    juce::AudioBuffer<float> samples;
    double inputRate = 44100, outputRate = 44100;
    int64_t onset = 0;
    void event(const juce::MidiMessage&);
    void renderRange(juce::AudioBuffer<float>&,int start,int count);
    void release(Voice&);
};
class PreviewOutput final : public juce::AudioSource
{
public:
    PreviewOutput(juce::AudioTransportSource& t,MidiSampler& s,Sf2KitSampler& f,Rex2SlicePlayer& r,
                  SampleAudition& a,ExternalKitSampler& e,TrackerSongRenderer& ts,
                  SampleVoicePlayer& sv)
        : transport(t),sampler(s),sf2(f),rex(r),audition(a),external(e),tracker(ts),sampleVoices(sv) {}
    void prepareToPlay(int,double) override;
    void releaseResources() override { transport.releaseResources(); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    MidiEventQueue collector;
    std::atomic<bool> midi {false};
    std::atomic<bool> sf2Mode {false};
    std::atomic<bool> externalMode {false};
    std::atomic<bool> rexMode {false};
    std::atomic<bool> trackerSongMode {false};
    std::atomic<bool> trackerSampleMidi {false};
    std::atomic<bool> discMidi {false};
    std::atomic<float> gain {0.7f};
    std::atomic<float> lastOutputPeak {0.0f};
private:
    juce::AudioTransportSource& transport;
    MidiSampler& sampler;
    Sf2KitSampler& sf2;
    Rex2SlicePlayer& rex;
    SampleAudition& audition;
    ExternalKitSampler& external;
    TrackerSongRenderer& tracker;
    SampleVoicePlayer& sampleVoices;
    juce::MidiBuffer messages;
    juce::AudioBuffer<float> midiBuffer;
};
}
