#pragma once
#include "core/scanner/FileScanner.h"
#include "core/rex/Rex2Decoder.h"
#include "core/sampler/SamplerTypes.h"
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
                             const juce::File& destination, CancelCheck cancel = {},
                             double fadeSeconds = 0) const;
    ExportResult exportSelection(const juce::File& source, double start, double end,
                                 const juce::File& folder, CancelCheck cancel = {}) const;
    // REX2 has no reader in AudioFormatManager, so a waveform selection is rendered from the
    // already-decoded PCM. The source file name is used for the WAV stem only.
    ExportResult exportRexSelection(const Rex2Audio& audio, const juce::File& source,
                                    double start, double end, const juce::File& folder,
                                    CancelCheck cancel = {}) const;
    // Decoded sampler PCM (SF2 and other built-in sources) has no AudioFormatReader. The range is
    // in seconds relative to this immutable source; sample rate, channels and source values are
    // preserved and no gain, transpose or normalisation is applied.
    ExportResult exportSampleSelection(const SampleAudio& audio, const juce::String& stem,
                                       double start, double end, const juce::File& folder,
                                       CancelCheck cancel = {}) const;
    // True when every channel holds only zeros. Used to keep silent embedded samples out of a
    // batch export; the test is exact, so very quiet but non-zero audio still counts as audio.
    static bool isEntirelySilent(const SampleAudio&);
    // Writes one whole decoded embedded sample to an exact path. No start/end range and no naming
    // policy: the caller owns the file name. With `normalize`, the peak of both channels is
    // scaled together to -0.1 dBFS. Never overwrites an existing destination.
    // `sampler` adds the root key and the loop to the WAV `smpl` chunk; null leaves the file bare.
    ExportResult exportSampleFile(const SampleAudio& audio, const juce::File& destination,
                                  bool normalize, CancelCheck cancel = {},
                                  const SamplerChunk* sampler = nullptr) const;
};
} // namespace sonicator
