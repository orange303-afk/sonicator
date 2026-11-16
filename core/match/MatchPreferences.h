#pragma once
#include <juce_core/juce_core.h>

namespace sonicator
{
// Target mode is a musical mode, not a runtime flag: Match Scale never converts minor to major,
// it only transposes within the mode the user picked. The names below are the stored values.
enum class TargetMode { major = 0, minor = 1 };

// One instance of the plug-in remembers this in the DAW project. The application never reads it:
// Match BPM / Match Scale exist only inside the host.
struct PluginMatchPreferences
{
    bool matchBpm = false;
    bool matchScale = false;
    int targetTonic = 0; // 0 = C .. 11 = B
    TargetMode targetMode = TargetMode::minor;

    bool operator== (const PluginMatchPreferences& other) const
    {
        return matchBpm == other.matchBpm && matchScale == other.matchScale
            && targetTonic == other.targetTonic && targetMode == other.targetMode;
    }
    bool operator!= (const PluginMatchPreferences& other) const { return ! (*this == other); }
};

inline juce::String tonicName (int tonic)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return tonic >= 0 && tonic < 12 ? names[tonic] : juce::String();
}

inline juce::String targetModeName (TargetMode mode)
{
    return mode == TargetMode::major ? "major" : "minor";
}

inline int normaliseTonic (int tonic) { return juce::jlimit (0, 11, tonic); }

inline TargetMode normaliseTargetMode (int stored)
{
    return stored == (int) TargetMode::major ? TargetMode::major : TargetMode::minor;
}
} // namespace sonicator
