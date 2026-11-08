#pragma once
#include "FileInstrument.h"
#include "core/sf2/Sf2TriggerTelemetry.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
namespace sonicator {
// Renders one external-file instrument (EXS24 or supported NKI) with its key/velocity zones.
// Loading/prepare are message-thread operations; render() is audio-thread only and allocates
// nothing. Decoded sources are immutable snapshots shared with the waveform and exporter.
class ExternalKitSampler
{
public:
    static constexpr int maxVoices = 128;
    bool load(const FileInstrument&, juce::String& error);
    void prepare(double sampleRate);
    void reset();
    void render(juce::AudioBuffer<float>&, int start, int count, const juce::MidiBuffer&);
    int activeVoices() const;
    bool isLoaded() const { return ! zones.empty(); }
    void setGeneration(juce::uint64 value) { generation = value; }
    TriggerTelemetry::Snapshot lastTrigger() const { return telemetry.read(); }
    int midiMonitorNote() const { return lastNote.load(std::memory_order_relaxed); }
    int midiMonitorLayers() const { return lastLayers.load(std::memory_order_relaxed); }
private:
    struct Voice
    {
        bool active = false, releasing = false, looping = false, oneShot = false;
        int note = 0, source = -1;
        double position = 0;
        int64_t end = 0, loopStart = 0, loopEnd = 0;
        double increment = 1;
        float envelope = 0, releaseStep = 0, velocity = 0;
    };
    std::array<Voice, maxVoices> voices;
    std::vector<ExternalZone> zones;
    std::vector<SampleAudioPtr> sources;
    double outputRate = 44100;
    juce::uint64 generation = 0;
    TriggerTelemetry telemetry;
    std::atomic<int> lastNote { -1 }, lastLayers { 0 };
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void release(Voice&);
    void renderRange(juce::AudioBuffer<float>&, int start, int count);
};
} // namespace sonicator
