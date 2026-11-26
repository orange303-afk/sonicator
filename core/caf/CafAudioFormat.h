#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstddef>
#include <vector>

namespace sonicator
{
// Decoded Core Audio Format data. Channels are deinterleaved; both vectors hold exactly `frames`
// values when `channels` is 2.
struct CafAudio
{
    int sampleRate = 0;
    int channels = 1;
    int bitsPerSample = 16;
    int64_t frames = 0;
    std::vector<float> left, right;
};

// Pure, bounded parser used by the AudioFormat reader and by tests. Packed linear PCM
// (8/16/24/32-bit integers, 32/64-bit float), mu-law, A-law and Apple IMA4 packets are decoded;
// ALAC, AAC and every other compressed codec is refused with a reason, and so is anything above
// two channels. Never reads outside `bytes`.
bool decodeCaf(const std::vector<std::byte>& bytes, CafAudio& out, juce::String& error);

// Reads Core Audio Format files through JUCE's format layer, so scanning, waveform, playback,
// analysis and drag-and-drop use the same paths as WAV/AIFF/EBL. Registration order matters on
// macOS: JUCE's CoreAudioFormat also claims `.caf`, and this reader must win there, or the same
// catalog would decode differently per platform.
class CafAudioFormat final : public juce::AudioFormat
{
public:
    CafAudioFormat();
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
