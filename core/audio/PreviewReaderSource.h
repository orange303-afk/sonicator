#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
namespace sonicator {
// Streaming preview source for one decoded file. It replaces AudioFormatReaderSource so a waveform
// selection can become the playback loop: the loop bounds and the loop flag are atomics, so a live
// drag updates them without touching the audio callback. Bounds are published end-last, the same
// pattern MidiSampler uses, so the audio thread never reads a half-updated range.
class PreviewReaderSource final : public juce::PositionableAudioSource
{
public:
    explicit PreviewReaderSource(std::unique_ptr<juce::AudioFormatReader> sourceReader);
    void prepareToPlay(int, double) override {}
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    void setNextReadPosition(juce::int64) override;
    juce::int64 getNextReadPosition() const override;
    juce::int64 getTotalLength() const override;
    void setLooping(bool) override;
    bool isLooping() const override { return looping.load(std::memory_order_relaxed); }
    // Absolute source frames. An invalid or empty range clears it. The range can be set while
    // audio is running; only the range flag decides whether playback wraps or stops at the end.
    void setLoopRange(double startSeconds, double endSeconds);
    void clearLoopRange();
    void setRangeLooping(bool enabled) { rangeLooping.store(enabled, std::memory_order_relaxed); }
private:
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::atomic<juce::int64> nextPlayPos { 0 };
    std::atomic<juce::int64> loopStart { 0 }, loopEnd { -1 };
    std::atomic<bool> looping { false }, rangeLooping { false };
};
} // namespace sonicator
