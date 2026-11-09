#pragma once
#include "core/dsp/AnalysisAudio.h"
#include "core/dsp/TonalAnalyzer.h"
#include "core/smart/SmartTypes.h"
#include <vector>
namespace sonicator {
// Metadata gates. A DefaultValue sampler root is a mapping default, not a measured pitch, so it
// does not suppress the DSP pitch stage.
bool hasValidKeyMetadata(const std::vector<SmartCandidate>&);
bool hasValidPitchOrRootMetadata(const std::vector<SmartCandidate>&);
bool typeIsDrum(const juce::String& type);
bool typeAllowsPitch(const juce::String& type);

struct TonalExtraction
{
    std::vector<SmartCandidate> candidates;
    juce::String status = "ok"; // ok, unsupported or error
    juce::String error;
};

// One read-only pass over the shared decode. The caller has already decided that a tonal pass is
// worth running; this function applies the per-field gates and returns no candidate when the
// evidence is insufficient.
TonalExtraction extractTonalCandidates(const AnalysisAudio&,
                                       const std::vector<SmartCandidate>& metadata,
                                       const juce::String& structure,
                                       const juce::String& type,
                                       const TonalOptions& = {});
} // namespace sonicator
