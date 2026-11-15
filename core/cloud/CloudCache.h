#pragma once
#include "CloudModel.h"

namespace sonicator::cloud
{
// The layout cache is an accelerator, never a source of truth: a missing, truncated or stale file
// is simply recomputed. The fingerprint in the file name and in the header must both match.
juce::File defaultCloudCacheFolder();
juce::File cloudCacheFile(uint64_t fingerprint, const juce::File& folder = defaultCloudCacheFolder());
bool writeCloudLayout(const CloudLayout&, const juce::File&);
std::optional<CloudLayout> readCloudLayout(const juce::File&, uint64_t expectedFingerprint);

// Keeps at most `keep` newest layout files; the folder itself is never deleted.
void pruneCloudCache(const juce::File& folder = defaultCloudCacheFolder(), int keep = 2);
}
