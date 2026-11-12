#include "SmartResolver.h"
#include "KeyNames.h"
#include "NameHeuristics.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace sonicator {
namespace {
bool usable(const SmartCandidate& c)
{
    return c.status == CandidateStatus::Valid || c.status == CandidateStatus::DefaultValue;
}
bool validOnly(const SmartCandidate& c) { return c.status == CandidateStatus::Valid; }
int rank(const SmartCandidate& c) { return smartSourcePriority(c.source); }
juce::String formatNumber(double value, int decimals = 2)
{
    return std::abs(value - std::round(value)) < 0.0001 ? juce::String((int) std::round(value))
                                                       : juce::String(value, decimals);
}
juce::String describe(const std::vector<SmartCandidate>& list)
{
    juce::StringArray parts;
    for (const auto& c : list)
    {
        juce::String value;
        switch (c.field)
        {
            case SmartField::Bpm: value = formatNumber(c.numericValue); break;
            case SmartField::Pitch:
            case SmartField::RootNote: value = formatNumber(c.numericValue); break;
            default: value = c.textValue; break;
        }
        parts.add(c.source + "=" + value);
    }
    return parts.joinIntoString("; ");
}
template <typename Predicate>
std::vector<SmartCandidate> select(const std::vector<SmartCandidate>& all, SmartField field, Predicate pred)
{
    std::vector<SmartCandidate> out;
    for (const auto& c : all) if (c.field == field && pred(c)) out.push_back(c);
    std::stable_sort(out.begin(), out.end(), [](const SmartCandidate& a, const SmartCandidate& b)
    {
        const int bySource = rank(a) - rank(b);
        return bySource != 0 ? bySource < 0 : a.source < b.source;
    });
    return out;
}
int sourceGroups(const std::vector<SmartCandidate>& list)
{
    std::set<juce::String> groups;
    for (const auto& c : list) groups.insert(smartSourceGroup(c.source));
    return (int) groups.size();
}
juce::String agreementNote(const std::vector<SmartCandidate>& list)
{
    return sourceGroups(list) > 1 ? " (independent sources agree)" : " (single source)";
}
void resolveText(const std::vector<SmartCandidate>& all,
                 SmartField field,
                 juce::String& value,
                 SmartState& state,
                 juce::String& source,
                 juce::String& explanation,
                 const juce::String& label)
{
    const auto list = select(all, field, [](const SmartCandidate& c)
    {
        return c.status == CandidateStatus::Valid && c.textValue.isNotEmpty();
    });
    if (list.empty())
    {
        state = SmartState::Unknown;
        return;
    }

    std::vector<SmartCandidate> primaryList;
    std::vector<SmartCandidate> genreCandidates;

    if (field == SmartField::Type)
    {
        std::vector<SmartCandidate> instrumentCandidates;
        for (const auto& c : list)
        {
            if (c.source == "path")
                genreCandidates.push_back(c);
            else
                instrumentCandidates.push_back(c);
        }

        if (! instrumentCandidates.empty())
            primaryList = std::move(instrumentCandidates);
        else
            primaryList = list;
    }
    else
    {
        primaryList = list;
    }

    std::set<juce::String> distinct;
    for (const auto& c : primaryList)
    {
        auto normalized = c.textValue.trim().toLowerCase();
        if (field == SmartField::Type)
        {
            auto canonical = canonicalSmartTypeToken(c.textValue.trim());
            if (canonical.isNotEmpty())
                normalized = canonical.toLowerCase();
        }
        distinct.insert(normalized);
    }
    value = primaryList.front().textValue;
    source = primaryList.front().source;
    state = distinct.size() == 1 ? SmartState::Resolved : SmartState::Ambiguous;

    juce::String note = label + ": " + describe(primaryList)
                      + (distinct.size() > 1 ? " (interpretations differ)" : agreementNote(primaryList));
    if (! genreCandidates.empty() && primaryList.size() != list.size())
    {
        note += " (genre context: " + describe(genreCandidates) + ")";
    }

    explanation += (explanation.isEmpty() ? "" : " ") + note;
}
bool isHalfDouble(double a, double b)
{
    if (a <= 0 || b <= 0) return false;
    const double ratio = a > b ? a / b : b / a;
    return std::abs(ratio - 2.0) < 0.02;
}
void resolveBpm(const std::vector<SmartCandidate>& all, SmartResult& out)
{
    const auto list = select(all, SmartField::Bpm, [](const SmartCandidate& c)
    {
        return validOnly(c) && std::isfinite(c.numericValue)
            && c.numericValue >= 20.0 && c.numericValue <= 400.0;
    });
    if (list.empty())
    {
        // A DSP pass that found competing grids stores them as Invalid candidates. That is not the
        // same as "never analysed": the tempo is ambiguous, and the evidence stays visible.
        std::vector<SmartCandidate> ambiguous;
        for (const auto& c : all)
            if (c.field == SmartField::Bpm && c.source == "dsp"
                && c.status == CandidateStatus::Invalid
                && std::isfinite(c.numericValue) && c.numericValue > 0)
                ambiguous.push_back(c);
        if (! ambiguous.empty())
        {
            out.bpm = 0;
            out.hasBpm = false;
            out.bpmState = SmartState::Ambiguous;
            out.bpmSource = "dsp";
            juce::StringArray parts;
            for (const auto& c : ambiguous) parts.add(formatNumber(c.numericValue));
            out.explanation += (out.explanation.isEmpty() ? "" : " ")
                + juce::String("bpm: ambiguous onset grid (") + parts.joinIntoString(" / ") + ")"
                + (ambiguous.front().limitation.isNotEmpty()
                       ? " (" + ambiguous.front().limitation + ")" : "");
            return;
        }
        out.bpmState = SmartState::Unknown;
        return;
    }
    const auto variable = std::find_if(list.begin(), list.end(), [](const SmartCandidate& c)
    {
        return c.variableTempo;
    });
    if (variable != list.end())
    {
        // A variable-tempo source cannot be reduced to one number without inventing one.
        out.bpm = 0;
        out.hasBpm = false;
        out.bpmState = SmartState::Ambiguous;
        out.bpmSource = variable->source;
        out.explanation += (out.explanation.isEmpty() ? "" : " ")
            + juce::String("bpm: variable tempo reported by ") + variable->source
            + " (" + describe(list) + ")";
        return;
    }
    const auto& chosen = list.front();
    out.bpm = chosen.numericValue;
    out.hasBpm = true;
    out.bpmSource = chosen.source;
    out.bpmState = SmartState::Resolved;
    std::vector<SmartCandidate> agreeing { chosen };
    bool halfDouble = false;
    for (const auto& c : list)
    {
        if (&c == &chosen) continue;
        if (std::abs(c.numericValue - chosen.numericValue) <= 1.0) agreeing.push_back(c);
        else if (isHalfDouble(c.numericValue, chosen.numericValue)) halfDouble = true;
        else out.bpmState = SmartState::Conflicting;
    }
    if (out.bpmState == SmartState::Resolved && halfDouble)
        out.bpmState = SmartState::Ambiguous;
    // DSP confidence is a documented heuristic score, so it is shown as a number with that
    // meaning rather than hidden inside the candidate.
    const juce::String confidence = chosen.source == "dsp" && chosen.auxValue > 0.0
                                        && chosen.auxValue <= 1.0
                                      ? ", confidence " + juce::String(chosen.auxValue, 2)
                                      : juce::String();
    out.explanation += (out.explanation.isEmpty() ? "" : " ") + juce::String("bpm: ")
        + describe(list) + confidence
        + (out.bpmState == SmartState::Resolved ? agreementNote(agreeing)
          : out.bpmState == SmartState::Ambiguous ? " (half/double-time ambiguity)"
                                                 : " (conflict: sources disagree)");
}
// "A" or "F#" names a tonic without a mode. parseKeyValue deliberately treats a bare note as
// major for callers such as ID3 TKEY; the resolver must not invent the mode for a filename token.
bool bareTonic(const juce::String& text)
{
    auto value = text.trim();
    if (value.startsWithIgnoreCase("key"))
    {
        value = value.substring(3);
        while (value.startsWithChar('-') || value.startsWithChar('_') || value.startsWithChar(' '))
            value = value.substring(1);
    }
    if (value.isEmpty()) return false;
    const auto letter = juce::CharacterFunctions::toUpperCase(value[0]);
    if (letter < 'A' || letter > 'G') return false;
    if (value.length() == 1) return true;
    return value.length() == 2 && (value[1] == '#' || value[1] == 'b');
}
bool relativeKeys(int tonicA, int tonicB, const juce::String& modeA, const juce::String& modeB){
    if (modeA == modeB || modeA.isEmpty() || modeB.isEmpty()) return false;
    const bool aMajor = modeA == "major", bMajor = modeB == "major";
    if (aMajor == bMajor) return false;
    // C major and A minor share the same pitch-class set.
    if (aMajor) return ((tonicA + 9) % 12) == tonicB;
    return ((tonicB + 9) % 12) == tonicA;
}
void resolveKey(const std::vector<SmartCandidate>& all, SmartResult& out)
{
    struct Entry { SmartCandidate candidate; MusicalKey key; };
    std::vector<Entry> entries;
    for (const auto& c : select(all, SmartField::Key, [](const SmartCandidate& c)
         { return c.status == CandidateStatus::Valid && c.textValue.isNotEmpty(); }))
    {
        if (auto key = parseKeyValue(c.textValue))
        {
            if (bareTonic(c.textValue))
                key->mode = "unknown";
            entries.push_back({ c, *key });
        }
    }
    if (entries.empty())
    {
        out.keyState = SmartState::Unknown;
        return;
    }
    const auto& chosen = entries.front();
    out.keyTonic = chosen.key.tonic;
    out.keyMode = chosen.key.mode == "unknown" ? juce::String() : chosen.key.mode;
    out.keySource = chosen.candidate.source;
    out.keyState = SmartState::Resolved;
    bool ambiguous = false;
    bool conflict = false;
    for (const auto& e : entries)
    {
        if (e.key.tonic == chosen.key.tonic
            && (e.key.mode == chosen.key.mode
                || e.key.mode == "unknown" || chosen.key.mode == "unknown"))
            continue;
        if (relativeKeys(e.key.tonic, chosen.key.tonic, e.key.mode, chosen.key.mode)) ambiguous = true;
        else if (e.key.tonic != chosen.key.tonic || e.key.mode != chosen.key.mode) conflict = true;
    }
    if (conflict) out.keyState = SmartState::Conflicting;
    else if (ambiguous) out.keyState = SmartState::Ambiguous;
    juce::StringArray parts;
    std::vector<SmartCandidate> used;
    for (const auto& e : entries) { parts.add(e.candidate.source + "=" + e.key.canonical); used.push_back(e.candidate); }
    out.explanation += (out.explanation.isEmpty() ? "" : " ") + juce::String("key: ")
        + parts.joinIntoString("; ")
        + (out.keyState == SmartState::Resolved ? agreementNote(used)
           : out.keyState == SmartState::Ambiguous ? " (relative-key ambiguity)" : " (conflict: sources disagree)");
}
void resolvePitch(const std::vector<SmartCandidate>& all, SmartResult& out)
{
    const auto list = select(all, SmartField::Pitch, [](const SmartCandidate& c)
    {
        return validOnly(c) && std::isfinite(c.numericValue)
            && c.numericValue >= 0.0 && c.numericValue <= 127.0;
    });
    if (list.empty())
    {
        out.pitchState = SmartState::Unknown;
        return;
    }
    const auto& chosen = list.front();
    const int nearest = (int) std::lround(chosen.numericValue);
    out.hasPitch = true;
    out.pitchMidi = nearest;
    out.pitchCents = (chosen.numericValue - nearest) * 100.0 + chosen.auxValue;
    out.pitchSource = chosen.source;
    out.pitchState = SmartState::Resolved;
    for (const auto& c : list)
        if (std::abs(c.numericValue - chosen.numericValue) > 0.5)
        {
            out.pitchState = SmartState::Ambiguous;
            break;
        }
    out.explanation += (out.explanation.isEmpty() ? "" : " ")
        + juce::String("pitch: ") + describe(list)
        + (out.pitchState == SmartState::Resolved ? agreementNote(list) : " (competing pitches)");
}
void resolveRoot(const std::vector<SmartCandidate>& all, SmartResult& out)
{
    const auto list = select(all, SmartField::RootNote, [](const SmartCandidate& c)
    {
        return usable(c) && std::isfinite(c.numericValue) && c.numericValue >= 0.0 && c.numericValue <= 127.0;
    });
    if (list.empty())
    {
        out.rootState = SmartState::Unknown;
        return;
    }
    const auto valid = std::find_if(list.begin(), list.end(), [](const SmartCandidate& c)
    {
        return c.status == CandidateStatus::Valid;
    });
    const auto& chosen = valid != list.end() ? *valid : list.front();
    out.rootNote = (int) std::lround(chosen.numericValue);
    out.rootSource = chosen.source;
    out.rootState = chosen.status == CandidateStatus::DefaultValue ? SmartState::Default : SmartState::Resolved;
    for (const auto& c : list)
        if (c.status == CandidateStatus::Valid
            && std::abs(c.numericValue - chosen.numericValue) > 0.5)
        {
            out.rootState = SmartState::Conflicting;
            break;
        }
    out.explanation += (out.explanation.isEmpty() ? "" : " ") + juce::String("root: ") + describe(list);
}
OverrideState overrideStateFor(const SmartOverride& value)
{
    if (value.cleared && value.notApplicable) return OverrideState::NotApplicable;
    if (value.cleared) return OverrideState::Unknown;
    return OverrideState::Value;
}
void applyOverride(const SmartOverrideMap& overrides, SmartField field, SmartResult& out)
{
    auto it = overrides.find(field);
    if (it == overrides.end()) return;
    const auto& value = it->second;
    const auto state = overrideStateFor(value);
    auto note = [&](const juce::String& label)
    {
        const char* kind = state == OverrideState::Value ? "value"
                         : state == OverrideState::NotApplicable ? "not applicable" : "unknown";
        out.explanation += (out.explanation.isEmpty() ? "" : " ")
            + juce::String("user override: ") + label + " = " + kind;
    };
    const bool cleared = state != OverrideState::Value;
    switch (field)
    {
        case SmartField::Type:
            out.typeOverride = state;
            if (cleared) out.type.clear(); else out.type = value.textValue;
            note("type"); break;
        case SmartField::Structure:
            out.structureOverride = state;
            if (cleared) out.structure.clear(); else out.structure = value.textValue;
            note("structure"); break;
        case SmartField::Bpm:
            out.bpmOverride = state;
            out.hasBpm = ! cleared && std::isfinite(value.numericValue)
                         && value.numericValue >= 20.0 && value.numericValue <= 400.0;
            if (! cleared) out.bpm = value.numericValue;
            out.bpmSource = out.hasBpm ? "override" : juce::String();
            note("bpm"); break;
        case SmartField::Key:
        {
            out.keyOverride = state;
            auto key = cleared ? std::nullopt : parseKeyValue(value.textValue);
            out.keyTonic = key ? key->tonic : -1;
            out.keyMode = key ? key->mode : juce::String();
            out.keySource = key ? "override" : juce::String();
            note("key"); break;
        }
        case SmartField::Pitch:
            out.pitchOverride = state;
            out.hasPitch = ! cleared && std::isfinite(value.numericValue)
                           && value.numericValue >= 0.0 && value.numericValue <= 127.0;
            out.pitchMidi = out.hasPitch ? (int) std::lround(value.numericValue) : -1;
            out.pitchCents = out.hasPitch ? (value.numericValue - std::round(value.numericValue)) * 100.0
                                            + value.auxValue
                                          : 0.0;
            out.pitchSource = out.hasPitch ? "override" : juce::String();
            note("pitch"); break;
        case SmartField::RootNote:
            out.rootOverride = state;
            out.rootNote = cleared ? -1 : (int) std::lround(value.numericValue);
            out.rootSource = out.rootNote >= 0 ? "override" : juce::String();
            note("root"); break;
    }
}
} // namespace
int smartSourcePriority(const juce::String& source)
{
    if (source == "user_adaptive") return -1;
    if (source == "global_adaptive" || source == "global_model") return 0;
    if (source == "rx2_recy") return 1;
    if (source == "rx2_glob") return 2;
    if (source == "wav_smpl" || source == "wav_inst") return 1;
    if (source == "aiff_basc") return 3;
    if (source == "wav_acid") return 4;
    if (source == "id3") return 4;
    if (source == "dsp_acoustic") return 5;
    if (source == "filename") return 6;
    if (source == "ml_classifier") return 7;
    if (source == "dsp") return 8;
    if (source == "asd_warp") return 9;
    return 11;
}
juce::String smartSourceGroup(const juce::String& source)
{
    if (source == "user_adaptive" || source == "global_adaptive" || source == "global_model") return "adaptive";
    if (source == "filename" || source == "folder" || source == "path") return "name";
    if (source == "asd_warp") return "sidecar";
    if (source == "dsp" || source == "dsp_acoustic" || source == "ml_classifier") return "dsp";
    if (source == "override") return "override";
    // Container metadata of one exporter is one evidence group, not several.
    return "container";
}
SmartResult resolveSmart(const std::vector<SmartCandidate>& candidates, const SmartOverrideMap& overrides)
{
    SmartResult out;
    resolveText(candidates, SmartField::Type, out.type, out.typeState, out.typeSource, out.explanation, "type");
    resolveText(candidates, SmartField::Structure, out.structure, out.structureState,
                out.structureSource, out.explanation, "structure");
    resolveBpm(candidates, out);
    resolveKey(candidates, out);
    resolvePitch(candidates, out);
    resolveRoot(candidates, out);
    applyOverride(overrides, SmartField::Type, out);
    applyOverride(overrides, SmartField::Structure, out);
    applyOverride(overrides, SmartField::Bpm, out);
    applyOverride(overrides, SmartField::Key, out);
    applyOverride(overrides, SmartField::Pitch, out);
    applyOverride(overrides, SmartField::RootNote, out);
    return out;
}
} // namespace sonicator
