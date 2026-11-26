#pragma once
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include "core/dsp/Spectrogram.h"

namespace sonicator
{
// Persistent overview of one file: the whole source reduced to a fixed log grid, plus a PNG of
// the same picture. The cache is an accelerator, never a source of truth: a missing, truncated or
// stale file is recomputed. The key carries path, size and mtime, and the header repeats it, so a
// hash collision can never show another file's spectrum.
struct SpectralOverview
{
    SpectrogramGrid grid;
    juce::Image image;
};

juce::File defaultSpectrumCacheFolder();
// Tests point this at a temporary folder so a component that builds its own cache path can never
// write into the owner's ~/Library/Sonicator/spectra. Empty resets.
void setSpectrumCacheFolderForTest(const juce::File& folder);

// "path|size|mtime|v1" — the identity both the file name and the header use.
juce::String spectrumCacheKey(const juce::File& source);
juce::File spectrumCacheFile(const juce::File& source, const juce::String& extension,
                             const juce::File& folder = defaultSpectrumCacheFolder());
// Both the .sps and the .png exist. A preview hit only needs the .sps, but the batch action also
// promises a PNG, so freshness means both.
bool spectrumCacheIsFresh(const juce::File& source,
                          const juce::File& folder = defaultSpectrumCacheFolder());
bool writeSpectrumCache(const juce::File& source, const SpectrogramGrid& grid,
                        const juce::File& folder = defaultSpectrumCacheFolder());
std::optional<SpectralOverview> readSpectrumCache(
    const juce::File& source, const juce::File& folder = defaultSpectrumCacheFolder());

// Keeps the `keep` newest entries and the total under `maxBytes`; deletes the matching .png with
// every .sps. The folder itself is never removed.
void pruneSpectrumCache(const juce::File& folder = defaultSpectrumCacheFolder(),
                        int keep = 2000, juce::int64 maxBytes = 512ll * 1024 * 1024);
int spectrumCacheFileCount(const juce::File& folder = defaultSpectrumCacheFolder());
juce::int64 spectrumCacheBytes(const juce::File& folder = defaultSpectrumCacheFolder());

// A fixed heat ramp for cached PNGs, independent of the live theme: a theme change must not
// invalidate the cache, and the card itself always draws from the float grid.
juce::Colour spectrumPngColour(float level);
juce::Image renderSpectrumPng(const SpectrogramGrid&);
} // namespace sonicator
