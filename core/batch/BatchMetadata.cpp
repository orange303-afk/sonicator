#include "BatchMetadata.h"

namespace sonicator::batch
{
namespace
{
const juce::StringArray& textWhitelist()
{
    // JUCE exposes RIFF INFO fields by their four-character chunk ids. The readable aliases are
    // kept for readers that normalize them.
    static const juce::StringArray keys {
        "INAM", "IART", "IPRD", "ICMT", "ICRD", "IGNR", "ISFT", "ICOP", "ISRC", "ITRK", "IPRT",
        "ISBJ", "IENG", "ITCH", "Title", "Artist", "Album", "Comment", "Genre", "Software"
    };
    return keys;
}

const juce::StringArray& samplerKeys()
{
    static const juce::StringArray keys { "MidiUnityNote", "MidiPitchFraction", "Manufacturer",
                                          "Product", "SamplePeriod", "SmpteFormat", "SmpteOffset" };
    return keys;
}

bool isTimeIndexed(const juce::String& key)
{
    return key.startsWith("Loop") || key == "NumSampleLoops" || key.startsWith("Cue")
        || key == "NumCuePoints" || key == "SamplerData" || key.startsWith("TimeReference")
        || key == "BWFTimeReference";
}
} // namespace

MetadataPlan prepareMetadata(const juce::AudioFormatReader& source, Format output, bool timeChanged)
{
    MetadataPlan plan;
    const auto& metadata = source.metadataValues;
    if (metadata.size() == 0) return plan;
    const bool sourceIsWav = source.getFormatName().containsIgnoreCase("WAV");
    if (! sourceIsWav || output != Format::wav)
    {
        plan.warnings.push_back({ ErrorCode::metadata_not_preserved,
                                  "Text and sampler metadata are not carried to "
                                      + formatLabel(output) + " in this version",
                                  true, {} });
        return plan;
    }
    int dropped = 0;
    for (int i = 0; i < metadata.size(); ++i)
    {
        const auto& key = metadata.getAllKeys()[i];
        if (textWhitelist().contains(key) || samplerKeys().contains(key))
        {
            plan.values.set(key, metadata.getAllValues()[i]);
            continue;
        }
        if (isTimeIndexed(key))
        {
            if (timeChanged) { ++dropped; continue; }
            plan.values.set(key, metadata.getAllValues()[i]);
        }
    }
    if (dropped > 0)
    {
        plan.timeIndexedDropped = true;
        plan.warnings.push_back({ ErrorCode::metadata_not_preserved,
                                  juce::String(dropped) + " time-indexed WAV chunk(s) were dropped "
                                      "because the audio length or rate changed",
                                  true, {} });
    }
    return plan;
}
} // namespace sonicator::batch
