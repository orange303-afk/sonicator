#pragma once
#include "core/scanner/FileScanner.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
namespace sonicator {
// One bounded PCM read shared by the rhythm and tonal analyses. The smart pipeline must not
// open the same sample twice: decoding is the dominant cost of the DSP stage.
struct AnalysisAudioOptions
{
    double maxSeconds = 60.0;
    int64_t maxDecodedBytes = 64ll * 1024 * 1024;
};
struct AnalysisAudio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0;
    bool truncated = false; // the source is longer than the analysed prefix
    juce::String error;
    bool ok() const { return error.isEmpty() && sampleRate > 0 && buffer.getNumSamples() > 0; }
};
// Reads only the analysed prefix through JUCE's format readers. No network and no cache files.
AnalysisAudio readAnalysisAudio(const juce::File&, const AnalysisAudioOptions& = {},
                                const CancelCheck& = {});
} // namespace sonicator
