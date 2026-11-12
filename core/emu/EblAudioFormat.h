#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
namespace sonicator {
// Reads E-mu Emulator X .ebl samples through JUCE's format layer, so every reader path
// (scan, analysis, preview, waveform, MIDI, export, split) sees them as ordinary audio.
class EblAudioFormat final : public juce::AudioFormat
{
public:
    EblAudioFormat();
    juce::Array<int> getPossibleSampleRates() override;
    juce::Array<int> getPossibleBitDepths() override;
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    juce::AudioFormatReader* createReaderFor(juce::InputStream* sourceStream,
                                             bool deleteStreamIfOpeningFails) override;
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor(
        std::unique_ptr<juce::OutputStream>& streamToWriteTo,
        const juce::AudioFormatWriterOptions& options) override;
};
} // namespace sonicator
