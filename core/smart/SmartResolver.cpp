#include "SmartResolver.h"
#include "KeyNames.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace sonicator {
namespace {
bool usable(const SmartCandidate& c) { return c.status == CandidateStatus::Valid || c.status == CandidateStatus::DefaultValue; }
bool validOnly(const SmartCandidate& c) { return c.status == CandidateStatus::Valid; }
int rank(const SmartCandidate& c) { return smartSourcePriority(c.source); }
juce::String describe(const std::vector<SmartCandidate>& list)
{
    juce::StringArray parts;
    for (const auto& c : list)
    {
        auto value = c.field == SmartField::Bpm
            ? (c.numericValue == std::floor(c.numericValue) ? juce::String((int) c.numericValue)
                                                           : juce::String(c.numericValue, 2))
            : (c.field == SmartField::RootNote ? juce::String((int) c.numericValue) : c.textValue);
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
    std::set<juce::String> distinct;
    for (const auto& c : list) distinct.insert(c.textValue.toLowerCase());
    value = list.front().textValue;
    source = list.front().source;
    state = distinct.size() == 1 ? SmartState::Resolved : SmartState::Ambiguous;
    explanation = label + ": " + describe(list) + (distinct.size() > 1 ? " (ambiguous)" : "");
}
void resolveBpm(const std::vector<SmartCandidate>& all, const SmartOverrideMap& overrides,
                SmartResult& out)
{
    const auto list = select(all, SmartField::Bpm, [](const SmartCandidate& c)
    {
        return validOnly(c) && std::isfinite(c.numericValue) && c.numericValue >= 20.0 && c.numericValue <= 400.0;
    });
    if (list.empty())
    {
        out.bpmState = SmartState::Unknown;
        return;
    }
    out.bpm = list.front().numericValue;
    out.hasBpm = true;
    out.bpmSource = list.front().source;
    out.bpmState = SmartState::Resolved;
    for (const auto& c : list)
        if (std::abs(c.numericValue - list.front().numericValue) > 1.0)
        {
            out.bpmState = SmartState::Conflicting;
            break;
        }
    out.explanation += (out.explanation.isEmpty() ? "" : " ") + juce::String("bpm: ") + describe(list)
                     + (out.bpmState == SmartState::Conflicting ? " (conflict)" : "");
    (void) overrides;
}
void resolveKey(const std::vector<SmartCandidate>& all, SmartResult& out)
{
    struct Entry { SmartCandidate candidate; MusicalKey key; };
    std::vector<Entry> entries;
    for (const auto& c : select(all, SmartField::Key, [](const SmartCandidate& c)
         { return c.status == CandidateStatus::Valid && c.textValue.isNotEmpty(); }))
    {
        if (auto key = parseKeyValue(c.textValue)) entries.push_back({ c, *key });
    }
    if (entries.empty())
    {
        out.keyState = SmartState::Unknown;
        return;
    }
    out.keyTonic = entries.front().key.tonic;
    out.keyMode = entries.front().key.mode;
    out.keySource = entries.front().candidate.source;
    out.keyState = SmartState::Resolved;
    for (const auto& e : entries)
        if (e.key.tonic != entries.front().key.tonic || e.key.mode != entries.front().key.mode)
        {
            out.keyState = SmartState::Conflicting;
            break;
        }
    juce::StringArray parts;
    for (const auto& e : entries) parts.add(e.candidate.source + "=" + e.key.canonical);
    out.explanation += (out.explanation.isEmpty() ? "" : " ") + juce::String("key: ")
                     + parts.joinIntoString("; ")
                     + (out.keyState == SmartState::Conflicting ? " (conflict)" : "");
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
void applyOverride(const SmartOverrideMap& overrides, SmartField field, SmartResult& out)
{
    auto it = overrides.find(field);
    if (it == overrides.end()) return;
    const auto& value = it->second;
    auto note = [&](const juce::String& label)
    {
        out.explanation += (out.explanation.isEmpty() ? "" : " ") + label + ": override";
    };
    switch (field)
    {
        case SmartField::Type:
            out.type = value.cleared ? juce::String() : value.textValue;
            out.typeState = value.cleared ? SmartState::Unknown : SmartState::Overridden;
            note("type"); break;
        case SmartField::Structure:
            out.structure = value.cleared ? juce::String() : value.textValue;
            out.structureState = value.cleared ? SmartState::Unknown : SmartState::Overridden;
            note("structure"); break;
        case SmartField::Bpm:
            out.hasBpm = ! value.cleared && std::isfinite(value.numericValue)
                         && value.numericValue >= 20.0 && value.numericValue <= 400.0;
            out.bpm = out.hasBpm ? value.numericValue : 0.0;
            out.bpmState = out.hasBpm ? SmartState::Overridden : SmartState::Unknown;
            out.bpmSource = "override";
            note("bpm"); break;
        case SmartField::Key:
        {
            auto key = value.cleared ? std::nullopt : parseKeyValue(value.textValue);
            out.keyTonic = key ? key->tonic : -1;
            out.keyMode = key ? key->mode : juce::String();
            out.keyState = key ? SmartState::Overridden : SmartState::Unknown;
            out.keySource = key ? "override" : juce::String();
            note("key"); break;
        }
        case SmartField::RootNote:
            out.rootNote = value.cleared ? -1 : (int) std::lround(value.numericValue);
            out.rootState = out.rootNote >= 0 ? SmartState::Overridden : SmartState::Unknown;
            out.rootSource = out.rootNote >= 0 ? "override" : juce::String();
            note("root"); break;
    }
}
} // namespace
int smartSourcePriority(const juce::String& source)
{
    if (source == "rx2_recy") return 0;
    if (source == "rx2_glob") return 1;
    if (source == "aiff_basc") return 2;
    if (source == "wav_acid") return 3;
    if (source == "id3") return 3;
    if (source == "filename") return 4;
    if (source == "dsp") return 5;
    if (source == "wav_smpl" || source == "wav_inst") return 0;
    if (source == "asd_warp") return 6;
    return 10;
}
SmartResult resolveSmart(const std::vector<SmartCandidate>& candidates, const SmartOverrideMap& overrides)
{
    SmartResult out;
    resolveText(candidates, SmartField::Type, out.type, out.typeState, out.typeSource, out.explanation, "type");
    resolveText(candidates, SmartField::Structure, out.structure, out.structureState,
                out.structureSource, out.explanation, "structure");
    resolveBpm(candidates, overrides, out);
    resolveKey(candidates, out);
    resolveRoot(candidates, out);
    applyOverride(overrides, SmartField::Type, out);
    applyOverride(overrides, SmartField::Structure, out);
    applyOverride(overrides, SmartField::Bpm, out);
    applyOverride(overrides, SmartField::Key, out);
    applyOverride(overrides, SmartField::RootNote, out);
    return out;
}
} // namespace sonicator
