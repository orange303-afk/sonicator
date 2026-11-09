#include "EmbeddedSamples.h"
#include "core/sf2/Sf2Bank.h"
#include "core/tracker/TrackerDocument.h"
namespace sonicator {
namespace {
juce::String entryName(const juce::String& raw, int ordinal)
{
    auto name = raw.trim();
    if (name.isEmpty()) return "Sample " + juce::String(ordinal);
    return name;
}
}
EmbeddedSampleSet::EmbeddedSampleSet() = default;
EmbeddedSampleSet::~EmbeddedSampleSet() = default;
bool isEmbeddedSampleSource(const juce::File& file)
{
    return file.hasFileExtension("mod;xm;s3m;sf2");
}
std::vector<juce::File> embeddedSources(const std::vector<juce::File>& files)
{
    std::vector<juce::File> out;
    for (const auto& file : files)
        if (file.existsAsFile() && isEmbeddedSampleSource(file)) out.push_back(file);
    return out;
}
bool EmbeddedSampleSet::open(const juce::File& file, juce::String& error)
{
    error.clear();
    kind = Kind::none;
    tracker.reset();
    bank.reset();
    listed.clear();
    empties = 0;
    if (! file.existsAsFile())
    {
        error = "File is missing";
        return false;
    }
    if (file.hasFileExtension("mod;xm;s3m"))
    {
        auto document = TrackerDocument::open(file, error);
        if (document == nullptr) return false;
        for (const auto& slot : document->slots())
        {
            if (slot.empty || slot.fmInstrument || slot.frames <= 0) { ++empties; continue; }
            listed.push_back({ entryName(slot.name.isNotEmpty() ? slot.name : slot.displayText,
                                         (int) listed.size() + 1),
                               slot.slot });
        }
        kind = Kind::tracker;
        tracker = std::move(document);
        return true;
    }
    if (file.hasFileExtension("sf2"))
    {
        auto loaded = std::make_unique<Sf2Bank>();
        if (! loaded->load(file, error)) return false;
        const auto& samples = loaded->samples();
        for (int i = 0; i < (int) samples.size(); ++i)
        {
            const auto& sample = samples[(size_t) i];
            // A right-channel header is decoded through its left partner, which is listed instead.
            if (sample.sampleType == 2) continue;
            if (sample.end <= sample.start) { ++empties; continue; }
            listed.push_back({ entryName(sample.name, (int) listed.size() + 1), i });
        }
        kind = Kind::sf2;
        bank = std::move(loaded);
        return true;
    }
    error = "Extract samples supports only MOD, XM, S3M and SF2";
    return false;
}
SampleAudioPtr EmbeddedSampleSet::read(int index, juce::String& error) const
{
    error.clear();
    if (kind == Kind::tracker && tracker != nullptr) return tracker->sampleAudio(index, error);
    if (kind == Kind::sf2 && bank != nullptr) return bank->sampleAudio((SampleId) index, error);
    error = "No embedded sample set is open";
    return {};
}
} // namespace sonicator
