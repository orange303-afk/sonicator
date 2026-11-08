#pragma once
#include "Sf2Bank.h"
#include "Sf2TriggerTelemetry.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
namespace sonicator {
// Renders one SF2 preset with the bank's original key/velocity mapping. Loading and reset are
// message-thread operations; render() is audio-thread only and allocates nothing.
class Sf2KitSampler
{
public:
    static constexpr int maxVoices = 128;
    bool load(const Sf2Bank&, int presetIndex, juce::String& error);
    void prepare(double sampleRate);
    void reset();
    void render(juce::AudioBuffer<float>&, int start, int count, const juce::MidiBuffer&);
    int activeVoices() const;
    const juce::String& presetName() const { return name; }
    int presetIndex() const { return preset; }
    bool isLoaded() const { return ! zones.empty() && ! pcm.empty(); }
    // Generation is the caller's instrument identity. It is copied into every telemetry snapshot
    // so the UI can discard a late notification from a bank that has already been replaced.
    void setGeneration(juce::uint64 generationValue) { generation = generationValue; }
    TriggerTelemetry::Snapshot lastTrigger() const { return telemetry.read(); }
    int midiMonitorNote() const { return lastNote.load(std::memory_order_relaxed); }
    int midiMonitorLayers() const { return lastLayers.load(std::memory_order_relaxed); }
private:
    struct Voice
    {
        bool active = false, releasing = false;
        int note = 0, exclusive = 0;
        int left = -1, right = -1;
        double position = 0, increment = 1;
        float envelope = 0, releaseStep = 0, velocity = 0;
        int64_t loopStart = 0, loopEnd = 0;
        bool looping = false;
    };
    std::array<Voice, maxVoices> voices;
    std::vector<Sf2Zone> zones;
    std::vector<Sf2Sample> samples;
    std::vector<float> pcm;
    double outputRate = 44100;
    int preset = -1;
    juce::String name;
    juce::uint64 generation = 0;
    TriggerTelemetry telemetry;
    std::atomic<int> lastNote { -1 }, lastLayers { 0 };
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void release(Voice&, double seconds = 2.0);
    void renderRange(juce::AudioBuffer<float>&, int start, int count);
};
} // namespace sonicator
