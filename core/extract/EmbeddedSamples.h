#pragma once
#include "core/sampler/SamplerTypes.h"
#include "core/kurzweil/KrzFile.h"
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
bool isTrackerModuleSource(const juce::File&);
// Pitch ratio (2^((firstNote - C4)/12)) of the first pattern note that triggers each sample.
// Modules whose samples are never triggered, and every SF2 bank, report 1.0. libxmp note 61 is C-4, the
// rate the tracker readers publish in SampleAudio::sampleRate.
SampleAudioPtr resampleForPlayback(const SampleAudio&, double ratio);
// Filters a selection down to the files the extract action can read: existing tracker modules and
// SoundFont banks. The library also treats those formats as metadata-only for scanning, so this
// decision must be independent of that split-candidate filter.
std::vector<juce::File> embeddedSources(const std::vector<juce::File>& files);
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
    // Slots and headers that hold no audio at all (empty tracker slots, FM instruments and
    // zero-length SF2 ranges). Reported as "empty" by the extract batch.
    int emptyCount() const { return empties; }
    SampleAudioPtr read(int index, juce::String& error) const;
    // 1.0 when the sample is never triggered or the source is an SF2 bank.
    double playbackRatio(int index) const;
    // Root key and loop of a Kurzweil entry; empty for tracker modules and SoundFonts.
    std::optional<SamplerChunk> samplerChunk(int index) const;
private:
    struct KrzEntry
    {
        int sampleObject = 0;   // index into Bank::samples
        int objectId = 0;       // the object's id, to detect a stale index
        int header = 0;
        bool stereo = false;
    };
    enum class Kind { none, tracker, sf2, krz };
    Kind kind = Kind::none;
    std::unique_ptr<class TrackerDocument> tracker;
    std::unique_ptr<class Sf2Bank> bank;
    std::unique_ptr<krz::Bank> kurzweil;
    std::vector<KrzEntry> kurzweilEntries;
    std::vector<EmbeddedSampleEntry> listed;
    std::vector<int> firstNotes;
    int empties = 0;
};
} // namespace sonicator
