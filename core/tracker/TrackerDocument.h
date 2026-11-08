#pragma once
#include "TrackerFormats.h"
#include "core/sampler/SamplerTypes.h"
#include <memory>
namespace sonicator {
// One opened tracker module. Parsing is bounded and read-only; PCM is decoded lazily through
// sampleAudio() and cached as immutable snapshots shared by the waveform, audition and exporter.
// All methods are message-thread only for one document (the audio thread never touches it).
class TrackerDocument
{
public:
    static std::unique_ptr<TrackerDocument> open(const juce::File&, juce::String& error);
    static constexpr int64_t maxBytes = 64 * 1024 * 1024;
    const TrackerDocumentInfo& info() const { return data.info; }
    const std::vector<TrackerSampleSlot>& slots() const { return data.slots; }
    const juce::File& file() const { return sourceFile; }
    // Decodes exactly one source. A slot whose stored error is non-empty is rejected with that
    // reason rather than being decoded as a different width/signedness.
    SampleAudioPtr sampleAudio(int slot, juce::String& error) const;
private:
    TrackerDocument() = default;
    juce::File sourceFile;
    TrackerDocumentData data;
    mutable std::vector<SampleAudioPtr> cache;
    mutable int64_t cachedBytes = 0;
    static constexpr int64_t maxCachedBytes = 256 * 1024 * 1024;
};
} // namespace sonicator
