#pragma once
#include "core/scanner/FileScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
namespace sonicator {
struct SilenceOptions
{
    double thresholdDb = -60.0;   // peak per 1 ms grain
    double minSilenceMs = 250.0;
    double minPartMs = 100.0;
    double edgePaddingMs = 5.0;
};
struct SplitPart
{
    double startSeconds = 0, endSeconds = 0;
};
struct SplitDetection
{
    std::vector<SplitPart> parts;
    int discardedShortParts = 0;   // non-silent fragments dropped by minPartMs
};
// Fewer than two parts means "no split": the caller leaves the source untouched.
SplitDetection detectSplitParts(juce::AudioFormatReader&, const SilenceOptions&, CancelCheck);
} // namespace sonicator
