#pragma once
#include "core/database/SampleRecord.h"
#include "core/dsp/RhythmAnalyzer.h"
#include "SmartTypes.h"
#include <vector>
namespace sonicator {
// One read-only DSP pass over a catalogued audio file. It never changes measured Phase 1 features
// and never invents a value: an undetermined analysis records that it ran and returns no candidate.
struct RhythmExtraction
{
    std::vector<SmartCandidate> candidates;
    juce::String status = "ok";  // ok, unsupported (no decodable audio) or error
    juce::String error;
};
RhythmExtraction extractRhythmCandidates(const SampleRecord&, const RhythmOptions& = {});
// True for formats the library can decode through JUCE's basic readers. Metadata-only containers
// (RX2, SF2, RePatch and disc images) are deliberately excluded.
bool hasDecodableAudioExtension(const juce::String& extensionWithoutDot);
} // namespace sonicator
