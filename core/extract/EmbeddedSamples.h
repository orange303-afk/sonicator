#pragma once
#include "core/sampler/SamplerTypes.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
namespace sonicator {
// One embedded audio source announced before any PCM is decoded. `index` is the reader-specific
// handle passed back to read(): a tracker slot number or a SoundFont sample header index.
struct EmbeddedSampleEntry
{
    juce::String name;
    int index = -1;
};
// Formats whose samples are packed inside the source file. Instruments that only reference
// external audio (EXS24, NKI) are deliberately excluded: nothing can be extracted without
// collecting files from elsewhere.
bool isEmbeddedSampleSource(const juce::File&);
// Opens one tracker module or SoundFont bank and enumerates its embedded samples. Empty slots,
// FM instruments without PCM, SF2 right-channel halves and samples with no audio range are not
// listed. Reading one entry decodes exactly that source; the set must outlive its audio pointers
// only as long as a caller needs to request the next entry.
class EmbeddedSampleSet
{
public:
    EmbeddedSampleSet();
    ~EmbeddedSampleSet();
    bool open(const juce::File&, juce::String& error);
    const std::vector<EmbeddedSampleEntry>& entries() const { return listed; }
    SampleAudioPtr read(int index, juce::String& error) const;
private:
    enum class Kind { none, tracker, sf2 };
    Kind kind = Kind::none;
    std::unique_ptr<class TrackerDocument> tracker;
    std::unique_ptr<class Sf2Bank> bank;
    std::vector<EmbeddedSampleEntry> listed;
};
} // namespace sonicator
