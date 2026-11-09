#include "core/dsp/FastAcousticRules.h"

namespace sonicator {

FastAcousticDecision FastAcousticRules::evaluate(const AcousticDescriptors& desc, double duration)
{
    // 1. Kick:
    // - Sub dominant (subBandEnergy[0] >= 0.40) OR (sub+low >= 0.70 with sub >= 0.30)
    // - Fast attack: logAttackTime <= -1.7 (~20 ms)
    // - Short/medium duration: duration <= 2.5s
    // - High crest factor: crestFactor >= 4.0
    // - Low zero-crossing rate: zeroCrossingRate < 2000
    const double sub = desc.subBandEnergy[0];
    const double low = desc.subBandEnergy[1];
    const bool kickSubDominant = (sub >= 0.40) || ((sub + low >= 0.70) && (sub >= 0.30));
    const bool kickFastAttack = (desc.logAttackTime <= -1.7);
    const bool kickDuration = (duration <= 2.5);
    const bool kickCrestFactor = (desc.crestFactor >= 4.0);
    const bool kickZcr = (desc.zeroCrossingRate < 2000.0);

    if (kickSubDominant && kickFastAttack && kickDuration && kickCrestFactor && kickZcr)
    {
        FastAcousticDecision decision;
        decision.determined = true;
        decision.canonicalType = "Kick";
        decision.confidence = 0.92;
        decision.ruleReason = "Sub-dominant transient with rapid attack";
        return decision;
    }

    // 2. Snare:
    // - Dual energy: body in Low/Mid (subBandEnergy[1] + subBandEnergy[2] >= 0.35)
    //   AND noise snap in High-Mid/Air (subBandEnergy[3] + subBandEnergy[4] >= 0.25)
    // - Fast attack: logAttackTime <= -1.7
    // - Duration <= 2.5s
    // - Moderate to high ZCR: zeroCrossingRate >= 1500
    // - Low pitch clarity: pitchClarity < 0.65
    const double snareBody = desc.subBandEnergy[1] + desc.subBandEnergy[2];
    const double snareSnap = desc.subBandEnergy[3] + desc.subBandEnergy[4];
    const bool snareDualEnergy = (snareBody >= 0.35) && (snareSnap >= 0.25);
    const bool snareFastAttack = (desc.logAttackTime <= -1.7);
    const bool snareDuration = (duration <= 2.5);
    const bool snareZcr = (desc.zeroCrossingRate >= 1500.0);
    const bool snarePitchClarity = (desc.pitchClarity < 0.65);

    if (snareDualEnergy && snareFastAttack && snareDuration && snareZcr && snarePitchClarity)
    {
        FastAcousticDecision decision;
        decision.determined = true;
        decision.canonicalType = "Snare";
        decision.confidence = 0.88;
        decision.ruleReason = "Dual-body and high-frequency noise transient";
        return decision;
    }

    // 3. Hat (Closed / Pedal):
    // - Air / High-Mid dominant: subBandEnergy[3] + subBandEnergy[4] >= 0.65
    // - Sub very low: subBandEnergy[0] < 0.08
    // - Fast attack: logAttackTime <= -1.8
    // - Fast decay / short duration: duration <= 1.2s and (decayRate >= 80 or duration <= 0.6s)
    // - High ZCR: zeroCrossingRate >= 3000
    const double highAirEnergy = desc.subBandEnergy[3] + desc.subBandEnergy[4];
    const bool hatHighDominant = (highAirEnergy >= 0.65);
    const bool hatSubLow = (desc.subBandEnergy[0] < 0.08);
    const bool hatFastAttack = (desc.logAttackTime <= -1.8);
    const bool hatDecayOrDuration = (duration <= 1.2) && (desc.decayRate >= 80.0 || duration <= 0.6);
    const bool hatHighZcr = (desc.zeroCrossingRate >= 3000.0);

    if (hatHighDominant && hatSubLow && hatFastAttack && hatDecayOrDuration && hatHighZcr)
    {
        FastAcousticDecision decision;
        decision.determined = true;
        decision.canonicalType = "Hat";
        decision.confidence = 0.92;
        decision.ruleReason = "High-frequency short transient";
        return decision;
    }

    // 4. Cymbal (Crash / Ride / Open):
    // - High/Air dominant: subBandEnergy[3] + subBandEnergy[4] >= 0.55
    // - Sub low: subBandEnergy[0] < 0.08
    // - Slower decay or longer duration: duration > 1.2s or (decayRate < 50 and duration > 0.8s)
    // - Low pitch clarity: pitchClarity < 0.6
    const bool cymbalHighDominant = (highAirEnergy >= 0.55);
    const bool cymbalSubLow = (desc.subBandEnergy[0] < 0.08);
    const bool cymbalDecayOrDuration = (duration > 1.2) || (desc.decayRate < 50.0 && duration > 0.8);
    const bool cymbalPitchClarity = (desc.pitchClarity < 0.6);

    if (cymbalHighDominant && cymbalSubLow && cymbalDecayOrDuration && cymbalPitchClarity)
    {
        FastAcousticDecision decision;
        decision.determined = true;
        decision.canonicalType = "Cymbal";
        decision.confidence = 0.88;
        decision.ruleReason = "High-frequency sustained texture";
        return decision;
    }

    // 5. Sub (Sub bass):
    // - Sub dominant: subBandEnergy[0] >= 0.60
    // - Very low air/high: subBandEnergy[3] + subBandEnergy[4] < 0.10
    // - High pitch clarity: pitchClarity >= 0.75
    // - Low ZCR: zeroCrossingRate < 300
    // - Slower attack or longer sustain: (logAttackTime > -2.0 or duration > 0.8s or decayRate < 40)
    const bool subDominant = (desc.subBandEnergy[0] >= 0.60);
    const bool subLowAirHigh = (highAirEnergy < 0.10);
    const bool subPitchClarity = (desc.pitchClarity >= 0.75);
    const bool subLowZcr = (desc.zeroCrossingRate < 300.0);
    const bool subSustain = (desc.logAttackTime > -2.0) || (duration > 0.8) || (desc.decayRate < 40.0);

    if (subDominant && subLowAirHigh && subPitchClarity && subLowZcr && subSustain)
    {
        FastAcousticDecision decision;
        decision.determined = true;
        decision.canonicalType = "Sub";
        decision.confidence = 0.90;
        decision.ruleReason = "Tonal pure low-frequency sustain";
        return decision;
    }

    // 6. Ambiguous / Melodic / Other
    return {};
}

} // namespace sonicator
