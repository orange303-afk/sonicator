#pragma once
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
// Maps an already computed analysis to candidates. The smart pipeline decodes once and uses this
// entry point so the rhythm pass does not open the file a second time.
RhythmExtraction rhythmCandidatesFrom(const RhythmAnalysis&);
// Decoding audio is the expensive part, and a reliable metadata tempo already answers the
// question. The DSP pass therefore runs only when no usable BPM candidate exists. Structure from
// audio is not requested on its own: the library treats DSP as a fallback, not a second opinion.
bool needsRhythmAnalysis(const std::vector<SmartCandidate>& metadataCandidates);
// True for formats the library can decode through JUCE's basic readers. Metadata-only containers
// (RX2, SF2, RePatch and disc images) are deliberately excluded.
bool hasDecodableAudioExtension(const juce::String& extensionWithoutDot);
} // namespace sonicator
