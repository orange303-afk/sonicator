#include "FeatureJson.h"
#include <juce_core/juce_core.h>

namespace sonicator {

juce::String featureJsonLine(const FeatureVectorV2& vector, const juce::String& path, double sampleRate,
                             double durationSeconds, double nyquistHz)
{
    juce::String line;
    line << "{\"path\":" << juce::JSON::toString(juce::var(path), false)
         << ",\"featureSchema\":" << FeatureExtractorV2::version()
         << ",\"dimensions\":" << FeatureExtractorV2::dimensions()
         << ",\"sampleRate\":" << juce::String(static_cast<int>(std::lround(sampleRate)))
         << ",\"durationSec\":" << juce::String(durationSeconds, 3)
         << ",\"nyquistHz\":" << juce::String(static_cast<int>(std::lround(nyquistHz)))
         << ",\"inBandEnergyShare\":" << juce::String(vector.inBandShare, 3)
         << ",\"features\":[";
    for (size_t i = 0; i < vector.values.size(); ++i)
    {
        if (i > 0) line << ",";
        line << juce::String(vector.values[i], 3);
    }
    line << "]}";
    return line;
}

} // namespace sonicator
