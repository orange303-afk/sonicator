#pragma once
#include "SmartTypes.h"
#include <vector>
namespace sonicator {
// Lower is stronger. The resolver keeps every source in the explanation, so priority only
// decides which value is shown when several agree or disagree.
int smartSourcePriority(const juce::String& source);
// Independent evidence group of a source. A file name and the metadata of the same exporter
// are not two confirmations: agreement is counted per group, not per row.
juce::String smartSourceGroup(const juce::String& source);
// Pure and deterministic: the same stored candidates and overrides always produce the same
// result, without file or database access.
SmartResult resolveSmart(const std::vector<SmartCandidate>& candidates, const SmartOverrideMap& overrides = {});
} // namespace sonicator
