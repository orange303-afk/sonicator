#pragma once
#include "core/dsp/AcousticFeatures.h"
#include <juce_core/juce_core.h>

namespace sonicator {

struct FastAcousticDecision {
    bool determined = false;
    juce::String canonicalType; // "Kick", "Snare", "Hat", "Cymbal", "Sub", "Percussion"
    double confidence = 0.0;    // 0.0 .. 1.0
    juce::String ruleReason;
};

class FastAcousticRules {
public:
    static FastAcousticDecision evaluate(const AcousticDescriptors& desc, double duration);
};

} // namespace sonicator
