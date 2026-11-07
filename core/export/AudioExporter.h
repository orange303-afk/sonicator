#pragma once
#include "core/scanner/FileScanner.h"
#include "core/rex/Rex2Decoder.h"
#include <juce_audio_formats/juce_audio_formats.h>
namespace sonicator {
struct ExportResult
{
    bool success = false;
    juce::File file;
    juce::String error;
};
// Writes a time range of an audio file to a WAV. Source files are opened read-only.
class AudioExporter
{
public:
    static constexpr juce::int64 exportChunkFrames = 65536;
    static constexpr int maxChannels = 64;
    static juce::File defaultExportFolder();
    static juce::String sanitizeStem(const juce::String& sourceFileNameWithoutExtension);
    static juce::String rangeSuffix(double startSeconds, double endSeconds);
    // Returns an empty string when the WAV has the expected format, frame count and payload bytes.
    static juce::String validateWavOutput(const juce::File&, juce::int64 expectedFrames,
                                          double sampleRate, int channels, int bits, bool floating);
    ExportResult exportRange(const juce::File& source, double start, double end,
                             const juce::File& destination, CancelCheck cancel = {}) const;
    ExportResult exportSelection(const juce::File& source, double start, double end,
                                 const juce::File& folder, CancelCheck cancel = {}) const;
    // REX2 has no reader in AudioFormatManager, so a waveform selection is rendered from the
    // already-decoded PCM. The source file name is used for the WAV stem only.
    ExportResult exportRexSelection(const Rex2Audio& audio, const juce::File& source,
                                    double start, double end, const juce::File& folder,
                                    CancelCheck cancel = {}) const;
};
} // namespace sonicator
