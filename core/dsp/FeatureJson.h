#pragma once
#include "FeatureExtractorV2.h"

namespace sonicator {

// One JSON object on one line, no trailing newline. The path is escaped like any JSON string; the
// values are rounded to three decimals, so a line is reproducible for the same input.
// `nyquistHz` is the file's own limit, not the content's band: `inBandEnergyShare` says how much of
// the sound actually falls inside the 40..8000 Hz analysis band.
juce::String featureJsonLine(const FeatureVectorV2& vector, const juce::String& path, double sampleRate,
                             double durationSeconds, double nyquistHz);

} // namespace sonicator
