#pragma once
#include "FeatureExtractorV2.h"

namespace sonicator {

// One JSON object on one line, no trailing newline. The path is escaped like any JSON string; the
// values are rounded to three decimals, so a line is reproducible for the same input.
juce::String featureJsonLine(const FeatureVectorV2& vector, const juce::String& path, double sampleRate,
                             double durationSeconds, double bandwidthHz);

} // namespace sonicator
