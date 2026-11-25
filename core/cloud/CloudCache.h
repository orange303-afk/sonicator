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

// The library a layout was built from, stored next to the layout file. A stale layout may only be
// shown when it belongs to the open catalog: another library's map must never flash on screen.
juce::File cloudLayoutCatalogFile(const juce::File& layoutFile);
bool writeCloudLayoutCatalog(const juce::File& layoutFile, const juce::File& catalogFile);
// The newest cached layout built from `catalog`, whatever its fingerprint says. This is what the
// window shows at launch while the background build checks whether the catalog actually changed,
// so a retagged library opens on the previous map instead of twenty seconds of "Building the map".
std::optional<CloudLayout> readNewestCloudLayout(const juce::File& folder, const juce::File& catalogFile);

// Keeps at most `keep` newest layout files; the folder itself is never deleted.
void pruneCloudCache(const juce::File& folder = defaultCloudCacheFolder(), int keep = 2);
}
