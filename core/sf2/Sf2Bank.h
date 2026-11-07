#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>
namespace sonicator {
struct Sf2Zone
{
    int keyLow = 0, keyHigh = 127, velocityLow = 0, velocityHigh = 127;
    int sampleIndex = -1;
    int rootKey = 60;          // MIDI key that plays the sample at its original pitch
    double tuningCents = 0;    // fine + coarse tune from the zone
    int loopMode = 0;          // 0 = no loop, 1 = continuous, 3 = loop until release
    int exclusiveClass = 0;    // choke group; 0 means none
    double attenuationDb = 0;
    double pan = 0;            // -1 left .. +1 right
};
struct Sf2Preset
{
    juce::String name;
    int bank = 0, program = 0;
    std::vector<Sf2Zone> zones;
};
struct Sf2Sample
{
    juce::String name;
    int64_t start = 0, end = 0, loopStart = 0, loopEnd = 0;
    int sampleRate = 0, originalPitch = 60, pitchCorrection = 0;
    int sampleLink = 0, sampleType = 0;
};
// Bounded, read-only SoundFont 2 parser for preview. It reads presets, instruments, zones and the
// 16-bit PCM payload; it does not need the optional 24-bit sm24 chunk.
class Sf2Bank
{
public:
    static constexpr int64_t maxFileSize = 256 * 1024 * 1024;
    bool load(const juce::File&, juce::String& error);
    bool isLoaded() const { return loaded; }
    const juce::String& name() const { return bankName; }
    const std::vector<Sf2Preset>& presets() const { return presetList; }
    const std::vector<Sf2Sample>& samples() const { return sampleList; }
    // Interleaved-free mono PCM in the bank's own 16-bit units, indexed by the sample headers.
    const std::vector<juce::int16>& pcm() const { return sampleData; }
    int firstDrumPreset() const;
    bool isDrumPreset(const Sf2Preset&) const;
    void clear();
private:
    bool loaded = false;
    juce::String bankName;
    std::vector<Sf2Preset> presetList;
    std::vector<Sf2Sample> sampleList;
    std::vector<juce::int16> sampleData;
};
} // namespace sonicator
