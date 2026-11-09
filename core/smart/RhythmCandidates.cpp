#include "RhythmCandidates.h"
#include <cmath>
namespace sonicator {
bool needsRhythmAnalysis(const std::vector<SmartCandidate>& metadataCandidates)
{
    for (const auto& candidate : metadataCandidates)
        if (candidate.field == SmartField::Bpm && candidate.status == CandidateStatus::Valid
            && std::isfinite(candidate.numericValue)
            && candidate.numericValue >= 20.0 && candidate.numericValue <= 400.0)
            return false;
    return true;
}
bool hasDecodableAudioExtension(const juce::String& extensionWithoutDot)
{
    const auto extension = extensionWithoutDot.toLowerCase();
    return extension == "wav" || extension == "aif" || extension == "aiff"
        || extension == "aifc" || extension == "mp3" || extension == "flac"
        || extension == "ogg" || extension == "oga" || extension == "m4a"
        || extension == "aac" || extension == "caf" || extension == "w64";
}
RhythmExtraction extractRhythmCandidates(const SampleRecord& record, const RhythmOptions& options)
{
    RhythmExtraction out;
    // Disc contents have no real path to decode; they are a preview action, not library analysis.
    if (record.isDiscEntry()) { out.status = "unsupported"; return out; }
    const auto extension = juce::File(record.filePath).getFileExtension().trimCharactersAtStart(".");
    if (! hasDecodableAudioExtension(extension)) { out.status = "unsupported"; return out; }
    const auto analysis = RhythmAnalyzer::analyzeFile(juce::File(record.filePath), options);
    if (analysis.error.isNotEmpty())
    {
        out.status = analysis.error.startsWith("unsupported") ? "unsupported" : "error";
        out.error = analysis.error;
        return out;
    }
    if (analysis.kind == RhythmKind::Undetermined) return out;
    SmartCandidate structure;
    structure.field = SmartField::Structure;
    structure.source = "dsp";
    structure.textValue = analysis.kind == RhythmKind::OneShot ? "one-shot" : "loop";
    structure.detail = analysis.explanation;
    structure.auxValue = analysis.kindConfidence;
    structure.limitation = "structure inferred from audio, not metadata";
    out.candidates.push_back(std::move(structure));
    if (analysis.kind != RhythmKind::RhythmicLoop || analysis.bpm <= 0) return out;
    SmartCandidate bpm;
    bpm.field = SmartField::Bpm;
    bpm.source = "dsp";
    bpm.numericValue = analysis.bpm;
    bpm.auxValue = analysis.bpmConfidence;
    bpm.detail = analysis.explanation;
    bpm.rawValue = juce::String(analysis.bpm, 2);
    bpm.limitation = "tempo estimated from audio; verify against the source";
    juce::StringArray alternatives;
    for (double value : analysis.alternatives) alternatives.add(juce::String(value, 1));
    if (! alternatives.isEmpty())
        bpm.limitation += "; alternatives: " + alternatives.joinIntoString(", ");
    // An ambiguous tempo is stored so the explanation survives, but never published as a value:
    // two competing grids are not one answer.
    bpm.status = analysis.tempo == RhythmTempo::Determined ? CandidateStatus::Valid
                                                           : CandidateStatus::Invalid;
    out.candidates.push_back(std::move(bpm));
    return out;
}
} // namespace sonicator
