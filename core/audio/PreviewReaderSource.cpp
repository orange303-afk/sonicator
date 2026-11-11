#include "PreviewReaderSource.h"
#include <cmath>
namespace sonicator {
PreviewReaderSource::PreviewReaderSource(std::unique_ptr<juce::AudioFormatReader> sourceReader)
    : reader(std::move(sourceReader)) {}
void PreviewReaderSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    if (reader == nullptr || info.numSamples <= 0) return;
    const auto total = reader->lengthInSamples;
    const auto end = loopEnd.load(std::memory_order_acquire);
    const auto start = loopStart.load(std::memory_order_relaxed);
    const bool hasRange = end > start && end > 0;
    // Live edits keep the playhead inside the range: an active loop that was started before the
    // range jumps into it instead of quietly playing the material around the selection.
    auto pos = nextPlayPos.load(std::memory_order_relaxed);
    if (pos < 0) pos = 0;
    const bool rangeActive = hasRange && rangeLooping.load(std::memory_order_relaxed);
    const bool loopNow = rangeActive || looping.load(std::memory_order_relaxed);
    if (rangeActive && (pos < start || pos >= end)) pos = start;
    const auto limit = hasRange ? juce::jmin(end, total) : total;
    int remaining = info.numSamples, offset = 0;
    while (remaining > 0)
    {
        if (pos >= limit)
        {
            if (! loopNow) break;
            pos = rangeActive ? start : 0;
            if (pos >= limit) break;
        }
        const auto count = (int) juce::jmin<juce::int64>(remaining, limit - pos);
        if (count <= 0) break;
        if (! reader->read(info.buffer, info.startSample + offset, count, pos, true, true))
            break;
        pos += count;
        offset += count;
        remaining -= count;
    }
    nextPlayPos.store(juce::jlimit<juce::int64>(0, juce::jmax<juce::int64>(0, total), pos),
                      std::memory_order_relaxed);
}
void PreviewReaderSource::setNextReadPosition(juce::int64 newPosition)
{
    nextPlayPos.store(juce::jmax<juce::int64>(0, newPosition), std::memory_order_relaxed);
}
juce::int64 PreviewReaderSource::getNextReadPosition() const
{
    return nextPlayPos.load(std::memory_order_relaxed);
}
juce::int64 PreviewReaderSource::getTotalLength() const
{
    return reader != nullptr ? reader->lengthInSamples : 0;
}
void PreviewReaderSource::setLooping(bool shouldLoop)
{
    looping.store(shouldLoop, std::memory_order_relaxed);
}
void PreviewReaderSource::setLoopRange(double startSeconds, double endSeconds)
{
    if (reader == nullptr || ! std::isfinite(startSeconds) || ! std::isfinite(endSeconds)
        || endSeconds <= startSeconds || reader->sampleRate <= 0)
    {
        clearLoopRange();
        return;
    }
    const auto frame = [this](double seconds)
    {
        return juce::jlimit<juce::int64>(0, reader->lengthInSamples,
            (juce::int64) std::llround(seconds * reader->sampleRate));
    };
    const auto start = frame(startSeconds), end = frame(endSeconds);
    if (end <= start) { clearLoopRange(); return; }
    loopEnd.store(-1, std::memory_order_release); // never publish a half-updated range
    loopStart.store(start, std::memory_order_relaxed);
    loopEnd.store(end, std::memory_order_release);
}
void PreviewReaderSource::clearLoopRange()
{
    loopEnd.store(-1, std::memory_order_release);
    loopStart.store(0, std::memory_order_relaxed);
}
} // namespace sonicator
