#pragma once
#include <juce_core/juce_core.h>
namespace sonicator {
// Version of the measured DSP feature set stored in SampleRecord: duration, rate, bits,
// channels, peak, rms, centroid, rolloff, flatness and the file hash. Bump it only when
// that output changes, and record the change in docs/ARCHITECTURE.md.
inline constexpr int basicFeaturesVersion = 1;
struct SampleRecord
{
    int64_t id = 0;
    juce::String filePath, fileName, fileHash, format, error;
    int64_t fileSize = 0, dateModified = 0, dateAnalyzed = 0;
    double duration = 0, sampleRate = 0;
    int bitsPerSample = 0, numChannels = 0, version = 0;
    double peakLevel = 0, rmsLevel = 0;
    double spectralCentroid = 0, spectralRolloff = 0, spectralFlatness = 0;
    bool missing = false;
};
}
