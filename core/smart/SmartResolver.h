#pragma once
#include "SmartTypes.h"
#include <vector>
namespace sonicator {
// Lower is stronger. The resolver keeps every source in the explanation, so priority only
// decides which value is shown when several agree or disagree.
int smartSourcePriority(const juce::String& source);
SmartResult resolveSmart(const std::vector<SmartCandidate>& candidates, const SmartOverrideMap& overrides = {});
} // namespace sonicator
