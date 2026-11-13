#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstddef>
#include <vector>
namespace sonicator {
// Decoded Creative Voice File data. Channels are deinterleaved; both vectors hold exactly
// `frames` values when `channels` is 2.
struct VocAudio
{
    int sampleRate = 0;
    int channels = 1;
    int bitsPerSample = 8;
    int64_t frames = 0;
    std::vector<float> left, right;
};
// Pure, bounded parser used by the AudioFormat reader and by tests. Returns false with a
// human-readable reason for a malformed or unsupported file; never reads outside `bytes`.
bool decodeVoc(const std::vector<std::byte>& bytes, VocAudio& out, juce::String& error);
// Reads Creative Voice Files through JUCE's format layer, so scanning, waveform, playback,
// analysis and drag-and-drop use the same paths as WAV/AIFF/EBL.
class VocAudioFormat final : public juce::AudioFormat
{
public:
    VocAudioFormat();
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
