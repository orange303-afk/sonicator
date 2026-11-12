#include "KrzInstrument.h"
#include "KrzFile.h"
#include <algorithm>
#include <map>
namespace sonicator {
namespace {
const char* const factoryRomMessage =
    "This bank uses the K2000 factory ROM samples; the file contains no samples of its own";

struct ZoneSource
{
    int sampleId = 0;
    int headerIndex = 0;
    bool operator< (const ZoneSource& other) const
    {
        return sampleId != other.sampleId ? sampleId < other.sampleId : headerIndex < other.headerIndex;
    }
};
struct SourceInfo
{
    int rootKey = 60;
    int headerIndex = 0;
    int objectId = 0;
};

bool sameEntry(const krz::KeymapEntry& a, const krz::KeymapEntry& b)
{
    return a.sampleId == b.sampleId && a.subSample == b.subSample && a.tuningCents == b.tuningCents
        && a.volumeHalfDb == b.volumeHalfDb;
}

// One zone per run of adjacent keys that share a keymap entry, per velocity level.
void appendKeymapZones(std::vector<ExternalZone>& out, const krz::Keymap& keymap,
                       const std::map<ZoneSource, int>& sourceOf, const FileInstrument& instrument,
                       const std::vector<SourceInfo>& sources, double pan, juce::StringArray& warnings,
                       const juce::String& context)
{
    for (int level = 0; level < 8; ++level)
    {
        const auto& entries = keymap.levels[(size_t) level];
        int note = 12;
        while (note < 128)
        {
            const auto& first = entries[(size_t) note];
            if (first.sampleId <= 0) { ++note; continue; }
            int last = note;
            while (last + 1 < 128 && sameEntry(entries[(size_t) (last + 1)], first)) ++last;
            const ZoneSource key { first.sampleId, std::max(0, first.subSample - 1) };
            const auto found = sourceOf.find(key);
            if (found == sourceOf.end())
            {
                const auto message = context + ": keymap " + juce::String(keymap.id)
                    + " refers to sample " + juce::String(first.sampleId) + " sub-sample "
                    + juce::String(first.subSample) + ", which has no audio in the file";
                if (! warnings.contains(message)) warnings.add(message);
            }
            else
            {
                const auto& info = sources[(size_t) found->second];
                const auto& descriptor = instrument.sources[(size_t) found->second];
                ExternalZone zone;
                zone.keyLow = note;
                zone.keyHigh = last;
                zone.velocityLow = level * 16;
                zone.velocityHigh = level == 7 ? 127 : level * 16 + 15;
                zone.sampleIndex = found->second;
                zone.rootKey = info.rootKey;
                zone.tuningCents = (double) first.tuningCents;
                zone.attenuationDb = -(double) first.volumeHalfDb / 2.0;
                zone.pan = pan;
                zone.start = 0;
                zone.end = descriptor.frameCount;
                zone.looping = descriptor.looping;
                zone.loopStart = descriptor.loopStart;
                zone.loopEnd = descriptor.loopEnd;
                out.push_back(zone);
            }
            note = last + 1;
        }
    }
}

void describeSources(FileInstrument& instrument, const std::vector<SourceInfo>& sources,
                     const krz::Bank& bank)
{
    for (size_t index = 0; index < instrument.sources.size(); ++index)
    {
        const auto& info = sources[index];
        auto& descriptor = instrument.sources[index];
        // Header volume applies to the whole source; the zone adds the keymap's own adjustment.
        descriptor.looping = false;
        for (const auto& sample : bank.samples)
        {
            if (sample.id != info.objectId) continue;
            if (info.headerIndex < (int) sample.headers.size())
            {
                const auto& header = sample.headers[(size_t) info.headerIndex];
                descriptor.looping = header.looping;
                descriptor.loopStart = 0;
                descriptor.loopEnd = 0;
            }
        }
        (void) info;
    }
}
} // namespace

bool loadKrzInstrument(const juce::File& file, FileInstrument& instrument, juce::String& error)
{
    error.clear();
    instrument = {};
    instrument.file = file;
    instrument.name = file.getFileNameWithoutExtension();
    krz::Bank bank;
    if (! krz::readBank(file, bank, error)) return false;
    if (! bank.hasPcm())
    {
        error = factoryRomMessage;
        return false;
    }
    instrument.warnings = bank.warnings;
    instrument.partial = bank.missingPart.isNotEmpty();

    std::map<ZoneSource, int> sourceOf;
    std::vector<SourceInfo> sourceInfo;
    for (const auto& sample : bank.samples)
    {
        if (sample.stereo && sample.headers.size() >= 2)
        {
            krz::DecodedSample decoded;
            juce::String decodeError;
            if (! krz::decodeStereo(bank, sample, decoded, decodeError))
            {
                instrument.warnings.add(sample.name.isEmpty() ? decodeError : sample.name + ": " + decodeError);
                continue;
            }
            const int index = (int) instrument.sources.size();
            SampleDescriptor descriptor;
            descriptor.id = index;
            descriptor.name = sample.name;
            descriptor.sampleRate = decoded.audio->sampleRate;
            descriptor.channels = decoded.audio->channels;
            descriptor.frameCount = decoded.audio->frames;
            descriptor.looping = decoded.looping;
            descriptor.loopStart = decoded.loopStart;
            descriptor.loopEnd = decoded.loopEnd;
            instrument.sources.push_back(std::move(descriptor));
            instrument.audio.push_back(decoded.audio);
            sourceInfo.push_back({ decoded.rootKey, 0, sample.id });
            sourceOf[ZoneSource { sample.id, 0 }] = index;
            sourceOf[ZoneSource { sample.id, 1 }] = index;
            continue;
        }
        for (int headerIndex = 0; headerIndex < (int) sample.headers.size(); ++headerIndex)
        {
            if (! sample.headers[(size_t) headerIndex].ramBased) continue;
            krz::DecodedSample decoded;
            juce::String decodeError;
            if (! krz::decodeHeader(bank, sample, headerIndex, decoded, decodeError))
            {
                instrument.warnings.add(sample.name.isEmpty() ? decodeError : sample.name + ": " + decodeError);
                continue;
            }
            const int index = (int) instrument.sources.size();
            SampleDescriptor descriptor;
            descriptor.id = index;
            descriptor.name = sample.headers.size() > 1
                ? sample.name + " " + juce::String(headerIndex + 1) : sample.name;
            descriptor.sampleRate = decoded.audio->sampleRate;
            descriptor.channels = decoded.audio->channels;
            descriptor.frameCount = decoded.audio->frames;
            descriptor.looping = decoded.looping;
            descriptor.loopStart = decoded.loopStart;
            descriptor.loopEnd = decoded.loopEnd;
            instrument.sources.push_back(std::move(descriptor));
            instrument.audio.push_back(decoded.audio);
            sourceInfo.push_back({ decoded.rootKey, headerIndex, sample.id });
            sourceOf[ZoneSource { sample.id, headerIndex }] = index;
        }
    }
    if (instrument.sources.empty())
    {
        error = factoryRomMessage;
        return false;
    }
    describeSources(instrument, sourceInfo, bank);

    auto keymapById = [&bank] (int id) -> const krz::Keymap*
    {
        for (const auto& keymap : bank.keymaps) if (keymap.id == id) return &keymap;
        return nullptr;
    };
    auto buildProgram = [&] (const juce::String& name, int number, const krz::Program* program)
    {
        InstrumentProgram built;
        built.name = name;
        built.number = number;
        if (program != nullptr)
        {
            const auto context = "Program " + program->name;
            for (const auto& layer : program->layers)
            {
                const auto* first = layer.keymap1 != 0 ? keymapById(layer.keymap1) : nullptr;
                const auto* second = layer.keymap2 != 0 ? keymapById(layer.keymap2) : nullptr;
                if (layer.keymap1 != 0 && first == nullptr)
                    instrument.warnings.add(context + ": keymap " + juce::String(layer.keymap1)
                                            + " is not in the file");
                if (layer.keymap2 != 0 && second == nullptr)
                    instrument.warnings.add(context + ": keymap " + juce::String(layer.keymap2)
                                            + " is not in the file");
                const double pan = second != nullptr ? -1.0 : 0.0;
                if (first != nullptr)
                    appendKeymapZones(built.zones, *first, sourceOf, instrument, sourceInfo, pan,
                                      instrument.warnings, context);
                if (second != nullptr)
                    appendKeymapZones(built.zones, *second, sourceOf, instrument, sourceInfo, 1.0,
                                      instrument.warnings, context);
            }
        }
        return built;
    };

    for (const auto& program : bank.programs)
    {
        auto built = buildProgram(program.name.isNotEmpty() ? program.name.trim()
                                                            : "Program " + juce::String(program.id),
                                  program.id, &program);
        if (! built.zones.empty()) instrument.programs.push_back(std::move(built));
    }
    if (instrument.programs.empty())
    {
        for (const auto& keymap : bank.keymaps)
        {
            InstrumentProgram built;
            built.name = keymap.name.isNotEmpty() ? keymap.name.trim()
                                                  : "Keymap " + juce::String(keymap.id);
            built.number = keymap.id;
            appendKeymapZones(built.zones, keymap, sourceOf, instrument, sourceInfo, 0.0, instrument.warnings,
                              "Keymap " + juce::String(keymap.id));
            if (! built.zones.empty()) instrument.programs.push_back(std::move(built));
        }
    }
    if (instrument.programs.empty())
    {
        for (size_t index = 0; index < instrument.sources.size(); ++index)
        {
            InstrumentProgram built;
            built.name = instrument.sources[index].name;
            built.number = (int) index;
            ExternalZone zone;
            zone.keyLow = 0;
            zone.keyHigh = 127;
            zone.velocityLow = 0;
            zone.velocityHigh = 127;
            zone.sampleIndex = (int) index;
            zone.rootKey = sourceInfo[index].rootKey;
            built.zones.push_back(zone);
            instrument.programs.push_back(std::move(built));
        }
    }
    if (instrument.programs.empty())
    {
        error = "This bank has no playable programs";
        return false;
    }
    juce::String selectError;
    if (! selectInstrumentProgram(instrument, 0, selectError))
    {
        error = selectError;
        return false;
    }
    return true;
}

bool selectInstrumentProgram(FileInstrument& instrument, int index, juce::String& error)
{
    error.clear();
    if (index < 0 || index >= (int) instrument.programs.size())
    {
        error = "Program index is out of range";
        return false;
    }
    instrument.zones = instrument.programs[(size_t) index].zones;
    instrument.currentProgram = index;
    for (auto& descriptor : instrument.sources) descriptor.zones.clear();
    for (const auto& zone : instrument.zones)
    {
        if (zone.sampleIndex < 0 || zone.sampleIndex >= (int) instrument.sources.size()) continue;
        SampleZoneRange range;
        range.keyLow = zone.keyLow;
        range.keyHigh = zone.keyHigh;
        range.velocityLow = zone.velocityLow;
        range.velocityHigh = zone.velocityHigh;
        auto& ranges = instrument.sources[(size_t) zone.sampleIndex].zones;
        if (std::find_if(ranges.begin(), ranges.end(), [&] (const SampleZoneRange& existing)
            { return existing.keyLow == range.keyLow && existing.keyHigh == range.keyHigh; }) == ranges.end())
            ranges.push_back(range);
    }
    return true;
}
} // namespace sonicator
