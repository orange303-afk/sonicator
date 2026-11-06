#pragma once
#include "core/scanner/FileScanner.h"
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
    ExportResult exportRange(const juce::File& source, double start, double end,
                             const juce::File& destination, CancelCheck cancel = {}) const;
    ExportResult exportSelection(const juce::File& source, double start, double end,
                                 const juce::File& folder, CancelCheck cancel = {}) const;
};
} // namespace sonicator
