#pragma once
#include "core/smart/SmartTypes.h"
#include <juce_core/juce_core.h>

namespace sonicator
{
// Tempo and root a dragged copy should carry. `hasRoot` is false when the row has no usable key.
struct DragMetadata
{
    bool hasBpm = false;
    double bpm = 0;
    bool hasRoot = false;
    int rootMidi = -1;
};

bool hasDragMetadata(const DragMetadata&);

// True for formats whose audio the shared decoder reads. EBL is excluded: ExternalFileDrag converts
// it separately because that path preserves its loop. REX2, soundfonts, tracker modules and disc
// images are excluded too, so they keep dragging as the original file.
bool canMaterialiseWithMetadata(const juce::File& source);

// Removes sibling cache folders older than `maxAgeDays`, keeping `keep`. Bounds the cache as tempo
// and root values change.
void pruneDragCache(const juce::File& cacheDir, const juce::File& keep, int maxAgeDays = 7);

// Manual or resolved BPM and key root from one row. Conflicting, ambiguous, unknown and stale
// values are dropped rather than written to a DAW as truth.
DragMetadata dragMetadataForDisplay(const SmartDisplay&);

// Source path when there is nothing to write; otherwise `<cacheDir>/<hash>/<stem>.wav`.
juce::File dragCopyPath(const juce::File& source, const DragMetadata&, const juce::File& cacheDir);

// Returns the source unchanged when there is nothing to write. WAV is byte-copied and spliced;
// other readable formats are decoded to WAV first. On failure `error` is set and the result is an
// invalid File. The source is never modified.
juce::File materialiseWithMetadata(const juce::File& source, const DragMetadata&,
                                   const juce::File& cacheDir, juce::String& error);
} // namespace sonicator
