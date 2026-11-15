#include "core/match/MatchPolicy.h"
#include <cmath>
#include <limits>

namespace sonicator
{
namespace
{
bool validBpm (double bpm) { return std::isfinite (bpm) && bpm > 0.0; }

bool trustworthy (MetadataState state) { return state == MetadataState::known; }

bool loopStructure (MatchStructure structure)
{
    return structure == MatchStructure::loop || structure == MatchStructure::rexLoop;
}

MatchPlan blocked (MatchPlan plan, MatchBlockReason reason)
{
    plan.action = MatchAction::blocked;
    plan.reason = reason;
    plan.tempoApplied = false;
    plan.pitchApplied = false;
    plan.timeRatio = 1.0;
    plan.semitones = 0.0;
    return plan;
}

juce::String lowered (const juce::String& text) { return text.trim().toLowerCase(); }
} // namespace

int shortestSemitones (int fromPitchClass, int toPitchClass)
{
    const int from = ((fromPitchClass % 12) + 12) % 12;
    const int to = ((toPitchClass % 12) + 12) % 12;
    int interval = ((to - from) % 12 + 12) % 12;
    if (interval > 6) interval -= 12;
    return interval;
}

MatchPlan resolveMatch (const MatchSource& source, const PluginMatchPreferences& preferences,
                       const plugin::HostTempoValue& hostTempo)
{
    MatchPlan plan;
    plan.targetTonic = preferences.targetTonic;
    plan.targetMode = preferences.targetMode;

    // Both off restores the ordinary preview path byte for byte, so nothing about the source
    // may block it, including a format the matcher would refuse.
    if (! preferences.matchBpm && ! preferences.matchScale)
        return plan;
    if (source.structure == MatchStructure::unsupported)
        return blocked (plan, MatchBlockReason::unsupportedSource);

    if (source.sourceFrames <= 0 || ! std::isfinite (source.sampleRate) || source.sampleRate <= 0.0)
        return blocked (plan, MatchBlockReason::invalidLength);

    // Tempo first: applicability, then the host/source values. A confirmed one-shot never needs
    // the host tempo, so a missing tempo must not block an independent pitch-only request.
    if (preferences.matchBpm)
    {
        if (loopStructure (source.structure))
        {
            if (! trustworthy (source.bpmState) || ! validBpm (source.bpm))
                return blocked (plan, MatchBlockReason::sourceBpmUnknown);
            if (! hostTempo.valid || ! validBpm (hostTempo.bpm))
                return blocked (plan, MatchBlockReason::hostTempoUnavailable);
            const double ratio = source.bpm / hostTempo.bpm;
            if (! std::isfinite (ratio) || ratio < 0.5 || ratio > 2.0)
                return blocked (plan, MatchBlockReason::ratioOutOfRange);
            plan.timeRatio = ratio;
            plan.tempoApplied = true;
            plan.sourceBpm = source.bpm;
            plan.targetBpm = hostTempo.bpm;
        }
        else if (source.structure == MatchStructure::oneShot)
        {
            plan.reason = MatchBlockReason::bpmNotApplicable;
        }
        else
        {
            plan.reason = MatchBlockReason::bpmStructureUnknown;
        }
    }

    if (preferences.matchScale)
    {
        const int targetTonic = ((preferences.targetTonic % 12) + 12) % 12;
        const bool hasKey = trustworthy (source.keyState) && source.keyTonic >= 0
                         && source.keyTonic < 12;
        const bool hasTonalRoot = ! hasKey
            && ((trustworthy (source.pitchState) && source.hasPitch && source.pitchMidi >= 0)
             || (trustworthy (source.rootState) && source.rootNote >= 0));
        if (hasKey)
        {
            // A pitch shift cannot turn A minor into C major. Refuse instead of producing audio
            // the user did not ask for; picking C minor is the honest alternative.
            const auto mode = lowered (source.keyMode);
            if (mode.isNotEmpty()
                && mode != (preferences.targetMode == TargetMode::major ? "major" : "minor"))
                return blocked (plan, MatchBlockReason::modeMismatch);
            plan.semitones = shortestSemitones (source.keyTonic, targetTonic);
            plan.pitchApplied = true;
        }
        else if (hasTonalRoot)
        {
            // A single note or a sampler root identifies a tonic, not a mode. Tune it to the
            // chosen tonic and say nothing about major/minor.
            const int pitchClass = source.hasPitch && trustworthy (source.pitchState)
                ? source.pitchMidi % 12 : source.rootNote % 12;
            plan.semitones = shortestSemitones (pitchClass, targetTonic);
            plan.pitchApplied = true;
        }
        else
        {
            return blocked (plan, MatchBlockReason::sourcePitchUnknown);
        }
    }

    if (! plan.tempoApplied && ! plan.pitchApplied) return plan; // bypass, with the honest reason

    if (! std::isfinite (plan.timeRatio) || plan.timeRatio <= 0.0)
        return blocked (plan, MatchBlockReason::ratioOutOfRange);
    const double frames = (double) source.sourceFrames * plan.timeRatio;
    if (! std::isfinite (frames) || frames <= 0.0
        || frames > (double) std::numeric_limits<int64_t>::max() / 2.0)
        return blocked (plan, MatchBlockReason::invalidLength);
    plan.targetFrames = (int64_t) std::llround (frames);
    if (plan.targetFrames <= 0) return blocked (plan, MatchBlockReason::invalidLength);

    plan.action = MatchAction::transform;
    plan.reason = MatchBlockReason::none;
    return plan;
}
} // namespace sonicator
