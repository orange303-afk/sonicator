#pragma once
#include "MatchTypes.h"
#include "plugin/HostTempoSnapshot.h"

namespace sonicator
{
// Pure: the same source, preferences and host snapshot always decide the same plan. No file, UI
// or database access, so preview and drag can both call it cheaply.
MatchPlan resolveMatch (const MatchSource& source, const PluginMatchPreferences& preferences,
                        const plugin::HostTempoValue& hostTempo);

// Signed shortest interval between two pitch classes in [-6, +6]; a tritone is deterministically
// +6. Both inputs are reduced modulo 12.
int shortestSemitones (int fromPitchClass, int toPitchClass);

inline double semitoneRatio (int semitones) { return std::pow (2.0, semitones / 12.0); }
} // namespace sonicator
