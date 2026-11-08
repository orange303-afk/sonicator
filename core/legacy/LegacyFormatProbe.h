#pragma once
#include "core/tracker/TrackerFormats.h"
namespace sonicator {
// Content-based recognition of the tracker containers this delivery can open. The extension is
// only a hint that makes probing cheap; it is never accepted as proof of a format.
struct LegacyFormatProbe
{
    TrackerFormat format = TrackerFormat::mod;
    bool recognised = false;
    juce::String variant, detail, error;
    int channels = 0, sampleSlots = 0;
    // Capabilities are per file and per source: a module can play while some of its instruments
    // are FM-only and therefore have no extractable PCM.
    bool songPlayback = false, sampleExtraction = false;
};
LegacyFormatProbe probeLegacyTracker(const juce::File&);
bool isLegacyTrackerExtension(const juce::File&);
} // namespace sonicator
