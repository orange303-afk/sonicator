#include "TonalCandidates.h"
#include "KeyNames.h"
#include <cmath>
namespace sonicator {
namespace {
bool usableNumber(const SmartCandidate& candidate)
{
    return candidate.status == CandidateStatus::Valid
        && std::isfinite(candidate.numericValue)
        && candidate.numericValue >= 0.0 && candidate.numericValue <= 127.0;
}
bool isOneShot(const juce::String& structure)
{
    const auto value = structure.trim().toLowerCase().replace("_", "-");
    return value == "one-shot" || value == "oneshot";
}
} // namespace

bool hasValidKeyMetadata(const std::vector<SmartCandidate>& metadata)
{
    for (const auto& candidate : metadata)
        if (candidate.field == SmartField::Key && candidate.status == CandidateStatus::Valid
            && candidate.textValue.trim().isNotEmpty())
            return true;
    return false;
}

bool hasValidPitchOrRootMetadata(const std::vector<SmartCandidate>& metadata)
{
    for (const auto& candidate : metadata)
    {
        if (candidate.field == SmartField::Pitch && usableNumber(candidate)) return true;
        if (candidate.field == SmartField::RootNote && usableNumber(candidate)) return true;
    }
    return false;
}

bool typeIsDrum(const juce::String& type)
{
    const auto value = type.trim().toLowerCase();
    return value == "kick" || value == "snare" || value == "clap" || value == "hat"
        || value == "percussion" || value == "cymbal";
}

bool typeAllowsPitch(const juce::String& type)
{
    const auto value = type.trim().toLowerCase();
    return ! typeIsDrum(value) && value != "vocal" && value != "fx";
}

TonalExtraction extractTonalCandidates(const AnalysisAudio& audio,
                                       const std::vector<SmartCandidate>& metadata,
                                       const juce::String& structure,
                                       const juce::String& type,
                                       const TonalOptions& options)
{
    TonalExtraction out;
    if (! audio.ok())
    {
        out.status = audio.error.startsWith("unsupported") ? "unsupported" : "error";
        out.error = audio.error.isNotEmpty() ? audio.error : "no decodable audio";
        return out;
    }
    const auto normalizedType = type.trim().toLowerCase();

    if (! hasValidKeyMetadata(metadata) && ! typeIsDrum(normalizedType))
    {
        const auto key = TonalAnalyzer::analyzeKey(audio.buffer, audio.sampleRate, options);
        if (key.error.isNotEmpty())
        {
            out.error = key.error;
            if (out.candidates.empty()) out.status = "error";
            if (key.error == "Cancelled") return out;
        }
        else if (key.found)
        {
            SmartCandidate candidate;
            candidate.field = SmartField::Key;
            candidate.source = "dsp";
            candidate.textValue = keyDisplayName(key.tonic, key.mode);
            candidate.detail = key.explanation;
            candidate.auxValue = key.confidence;
            candidate.rawValue = juce::String(key.bestCorrelation, 3);
            candidate.limitation = "key estimated from audio; profiles assume tonal material";
            out.candidates.push_back(std::move(candidate));
        }
    }

    if (isOneShot(structure) && typeAllowsPitch(normalizedType)
        && ! hasValidPitchOrRootMetadata(metadata))
    {
        const auto pitch = TonalAnalyzer::analyzePitch(audio.buffer, audio.sampleRate, options);
        if (pitch.error.isNotEmpty())
        {
            if (out.error.isEmpty()) out.error = pitch.error;
            if (out.candidates.empty()) out.status = "error";
            if (pitch.error == "Cancelled") return out;
        }
        else if (pitch.found)
        {
            SmartCandidate candidate;
            candidate.field = SmartField::Pitch;
            candidate.source = "dsp";
            candidate.numericValue = pitch.midi;
            candidate.auxValue = 0.0; // the resolver derives cents from the fractional MIDI
            candidate.textValue = juce::String(pitch.midi, 2);
            candidate.detail = pitch.explanation;
            candidate.rawValue = juce::String(pitch.midi, 2);
            candidate.limitation =
                "pitch estimated from a stable segment of a one-shot; cents are equal-tempered";
            out.candidates.push_back(std::move(candidate));
        }
    }
    return out;
}
} // namespace sonicator
