#pragma once
#include "AssetCompatibility.h"
#include <juce_core/juce_core.h>
namespace sonicator {
// Version of the measured DSP feature set stored in SampleRecord: duration, rate, bits,
// channels, peak, rms, centroid, rolloff, flatness and the file hash. Bump it only when
// that output changes, and record the change in docs/ARCHITECTURE.md.
inline constexpr int basicFeaturesVersion = 1;
// RX2/SF2 are indexed as library assets, not decoded audio: the row carries file identity and the
// metadata extracted by core/smart, so it is never mistaken for a measured Phase 1 result.
inline constexpr int metadataOnlyVersion = -1;
struct SampleRecord
{
    int64_t id = 0;
    juce::String filePath, fileName, fileHash, format, error;
    int64_t fileSize = 0, dateModified = 0, dateAnalyzed = 0;
    double duration = 0, sampleRate = 0;
    int bitsPerSample = 0, numChannels = 0, version = 0;
    double peakLevel = 0, rmsLevel = 0;
    double spectralCentroid = 0, spectralRolloff = 0, spectralFlatness = 0;
    bool missing = false, starred = false;
    AssetCompatibility compatibility = AssetCompatibility::unknown;
    // A sample inside a disc image. The path is synthetic (<image>::<internal path>); the source
    // file itself is never extracted. Ordinary files leave these empty.
    juce::String containerPath, discEntry, discParent, discPartner, discPath;
    bool isDiscEntry() const { return containerPath.isNotEmpty(); }
};
}
