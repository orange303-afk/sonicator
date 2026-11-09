#include "TonalCandidates.h"
#include "KeyNames.h"
#include <cmath>
#include <set>
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

bool pathIsPercussive(const juce::String& path)
{
    static const std::set<juce::String> percussionTokens {
        "drum", "drums", "perc", "percussion", "kick", "kicks", "snare", "snares",
        "hat", "hats", "hihat", "cymbal", "cymbals", "crash", "crashes", "ride", "rides",
        "tom", "toms", "clap", "claps", "rim", "rimshot", "shaker", "tambourine",
        "conga", "congas", "bongo", "bongos", "djembe", "tabla", "darbuka", "daf",
        "riq", "bendir", "break", "breaks", "breakbeat"
    };
    const juce::File file(path);
    const auto text = file.getFileNameWithoutExtension().toLowerCase() + " "
                    + file.getParentDirectory().getFileName().toLowerCase();
    juce::String normalized;
    for (auto c : text)
        normalized += juce::CharacterFunctions::isLetterOrDigit(c)
                          ? juce::String::charToString(c) : juce::String(" ");
    const auto tokens = juce::StringArray::fromTokens(normalized, " ", "");
    for (const auto& token : tokens)
        if (percussionTokens.count(token) != 0) return true;
    return false;
}

TonalExtraction extractTonalCandidates(const AnalysisAudio& audio,
                                       const std::vector<SmartCandidate>& metadata,
                                       const juce::String& structure,
                                       const juce::String& type,
                                       const juce::String& path,
                                       const CancelCheck& cancel,
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
    const bool percussivePath = path.isNotEmpty() && pathIsPercussive(path);
    const juce::String decodedNote = audio.truncated
        ? " (first " + juce::String((double) audio.buffer.getNumSamples() / audio.sampleRate, 0)
              + " s decoded)" : juce::String();

    if (! hasValidKeyMetadata(metadata) && ! typeIsDrum(normalizedType)
        && ! percussivePath && ! isOneShot(structure))
    {
        const auto key = TonalAnalyzer::analyzeKey(audio.buffer, audio.sampleRate, options,
                                                   cancel);
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
            candidate.extractorId = "dsp";
            candidate.extractorVersion = smartDspVersion;
            candidate.limitation = "key estimated from audio; profiles assume tonal material"
                                   + decodedNote;
            out.candidates.push_back(std::move(candidate));
        }
    }

    if (isOneShot(structure) && typeAllowsPitch(normalizedType)
        && ! percussivePath && ! hasValidPitchOrRootMetadata(metadata))
    {
        const auto pitch = TonalAnalyzer::analyzePitch(audio.buffer, audio.sampleRate, options,
                                                       cancel);
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
            candidate.extractorId = "dsp";
            candidate.extractorVersion = smartDspVersion;
            candidate.limitation =
                "pitch estimated from a stable segment of a one-shot; cents are equal-tempered"
                + decodedNote;
            out.candidates.push_back(std::move(candidate));
        }
    }
    return out;
}
} // namespace sonicator
