#include "Sf2Bank.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace sonicator {
namespace {
constexpr int maxPresets = 2048, maxInstruments = 4096, maxZones = 65536, maxSamples = 65536;
juce::uint16 le16(const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
juce::uint32 le32(const juce::uint8* p)
{
    return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
         | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
}
struct Span { const juce::uint8* data = nullptr; size_t size = 0; };
bool idIs(const juce::uint8* p, const char* id) { return std::memcmp(p, id, 4) == 0; }
struct Range { int low = 0, high = 127; };
Range keyRange(juce::uint16 amount) { return { amount & 0x7f, (amount >> 8) & 0x7f }; }
int16_t signed16(juce::uint16 value) { return (int16_t) value; }
struct InstrumentZone
{
    Sf2Zone zone;
    bool hasSample = false;
};
juce::String readName(const juce::uint8* data, size_t maxLength)
{
    size_t length = 0;
    while (length < maxLength && data[length] != 0) ++length;
    return juce::String::fromUTF8((const char*) data, (int) length).trim();
}
struct ResolvedSample
{
    int left = -1, right = -1;
    int64_t start = 0, frames = 0, loopStart = 0, loopEnd = 0;
    int channels = 0, rate = 0;
    bool looping = false;
    juce::String error;
};
// Resolves a sample header to one mono/stereo audio source. Stereo pairs are keyed by the left
// header so a zone that references either side still maps to one descriptor.
ResolvedSample resolveSample(const std::vector<Sf2Sample>& samples,
                             const std::vector<juce::int16>& pcm, int index)
{
    ResolvedSample resolved;
    if (index < 0 || index >= (int) samples.size())
    {
        resolved.error = "SoundFont sample index is out of range";
        return resolved;
    }
    const auto& sample = samples[(size_t) index];
    int left = index, right = -1;
    if (sample.sampleType == 2)
    {
        if (sample.sampleLink >= (int) samples.size() || sample.sampleLink == index)
        {
            resolved.error = "SoundFont right sample has no valid left link";
            return resolved;
        }
        left = sample.sampleLink;
        right = index;
    }
    else if (sample.sampleType == 4 && sample.sampleLink != 0)
    {
        if (sample.sampleLink < 0 || sample.sampleLink >= (int) samples.size()
            || sample.sampleLink == index)
        {
            resolved.error = "SoundFont left sample has an invalid right link";
            return resolved;
        }
        right = sample.sampleLink;
    }
    const auto& leftSample = samples[(size_t) left];
    if (leftSample.start < 0 || leftSample.end <= leftSample.start
        || leftSample.end > (int64_t) pcm.size())
    {
        resolved.error = "SoundFont sample bounds are outside the PCM data";
        return resolved;
    }
    if (leftSample.sampleRate <= 0)
    {
        resolved.error = "SoundFont sample has an invalid sample rate";
        return resolved;
    }
    resolved.left = left;
    resolved.right = right;
    resolved.start = leftSample.start;
    resolved.frames = leftSample.end - leftSample.start;
    resolved.rate = leftSample.sampleRate;
    resolved.channels = 1;
    if (right >= 0)
    {
        const auto& rightSample = samples[(size_t) right];
        if (rightSample.start < 0 || rightSample.end <= rightSample.start
            || rightSample.end > (int64_t) pcm.size()
            || rightSample.end - rightSample.start != resolved.frames)
        {
            resolved.error = "SoundFont stereo pair has mismatched bounds";
            return resolved;
        }
        if (rightSample.sampleRate != leftSample.sampleRate)
        {
            resolved.error = "SoundFont stereo pair has mismatched sample rates";
            return resolved;
        }
        resolved.channels = 2;
    }
    // The kit sampler clamps loop bounds to the usable frame range; descriptors report the same.
    resolved.loopStart = juce::jlimit(leftSample.start, leftSample.end, leftSample.loopStart)
                       - leftSample.start;
    resolved.loopEnd = juce::jlimit(leftSample.start, leftSample.end, leftSample.loopEnd)
                     - leftSample.start;
    resolved.looping = resolved.loopEnd > resolved.loopStart;
    return resolved;
}
} // namespace
void Sf2Bank::clear()
{
    loaded = false; bankName.clear(); presetList.clear(); sampleList.clear(); sampleData.clear();
}
bool Sf2Bank::isDrumPreset(const Sf2Preset& preset) const
{
    if (preset.bank == 128) return true;
    const auto lower = preset.name.toLowerCase();
    return lower.contains("drum") || lower.contains("kit") || lower.contains("perc")
        || lower.contains("beat");
}
int Sf2Bank::firstDrumPreset() const
{
    for (size_t i = 0; i < presetList.size(); ++i)
        if (isDrumPreset(presetList[i])) return (int) i;
    return -1;
}
bool Sf2Bank::load(const juce::File& file, juce::String& error)
{
    clear();
    if (! file.existsAsFile()) { error = "SoundFont file is missing"; return false; }
    if (file.getSize() > maxFileSize) { error = "SoundFont exceeds the 256 MiB preview limit"; return false; }
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block)) { error = "Cannot read SoundFont"; return false; }
    const auto* bytes = (const juce::uint8*) block.getData();
    const auto size = (size_t) block.getSize();
    if (size < 12 || ! idIs(bytes, "RIFF") || ! idIs(bytes + 8, "sfbk"))
    {
        error = "Not a SoundFont 2 bank"; return false;
    }
    Span info, pdta, sdta;
    size_t position = 12;
    int guard = 0;
    while (position + 8 <= size && ++guard < 1024)
    {
        const auto* header = bytes + position;
        const auto chunkSize = (size_t) le32(header + 4);
        const size_t body = position + 8;
        if (chunkSize > size - body) { error = "SoundFont chunk exceeds the file"; return false; }
        if (idIs(header, "LIST") && chunkSize >= 4)
        {
            if (idIs(bytes + body, "pdta")) pdta = { bytes + body + 4, chunkSize - 4 };
            else if (idIs(bytes + body, "sdta")) sdta = { bytes + body + 4, chunkSize - 4 };
            else if (idIs(bytes + body, "INFO")) info = { bytes + body + 4, chunkSize - 4 };
        }
        position = body + chunkSize + (chunkSize & 1);
    }
    if (pdta.data == nullptr) { error = "SoundFont has no pdta chunk"; return false; }
    // INFO name and sdta PCM.
    {
        size_t p = 0;
        while (p + 8 <= info.size)
        {
            const auto chunkSize = (size_t) le32(info.data + p + 4);
            if (chunkSize > info.size - p - 8) break;
            if (idIs(info.data + p, "INAM"))
                bankName = readName(info.data + p + 8, chunkSize);
            p += 8 + chunkSize + (chunkSize & 1);
        }
    }
    if (sdta.data != nullptr)
    {
        size_t p = 0;
        while (p + 8 <= sdta.size)
        {
            const auto chunkSize = (size_t) le32(sdta.data + p + 4);
            if (chunkSize > sdta.size - p - 8) break;
            if (idIs(sdta.data + p, "smpl"))
            {
                const auto frames = chunkSize / 2;
                sampleData.resize(frames);
                for (size_t i = 0; i < frames; ++i)
                    sampleData[i] = (juce::int16) le16(sdta.data + p + 8 + i * 2);
                break;
            }
            p += 8 + chunkSize + (chunkSize & 1);
        }
    }
    Span phdr, pbag, pgen, inst, ibag, igen, shdr;
    {
        size_t p = 0;
        while (p + 8 <= pdta.size)
        {
            const auto chunkSize = (size_t) le32(pdta.data + p + 4);
            if (chunkSize > pdta.size - p - 8) { error = "SoundFont pdta chunk is truncated"; return false; }
            const auto* id = pdta.data + p;
            const Span span { pdta.data + p + 8, chunkSize };
            if (idIs(id, "phdr")) phdr = span;
            else if (idIs(id, "pbag")) pbag = span;
            else if (idIs(id, "pgen")) pgen = span;
            else if (idIs(id, "inst")) inst = span;
            else if (idIs(id, "ibag")) ibag = span;
            else if (idIs(id, "igen")) igen = span;
            else if (idIs(id, "shdr")) shdr = span;
            p += 8 + chunkSize + (chunkSize & 1);
        }
    }
    if (phdr.size < 38 || shdr.size < 46 || inst.size < 22 || pbag.size < 4 || ibag.size < 4)
    {
        error = "SoundFont pdta is incomplete"; return false;
    }
    const auto sampleCount = (int) (shdr.size / 46) - 1;
    if (sampleCount <= 0) { error = "SoundFont has no samples"; return false; }
    if (sampleCount > maxSamples) { error = "SoundFont sample count is invalid"; return false; }
    for (int i = 0; i < sampleCount; ++i)
    {
        const auto* record = shdr.data + (size_t) i * 46;
        Sf2Sample sample;
        sample.name = readName(record, 20);
        sample.start = (int64_t) le32(record + 20);
        sample.end = (int64_t) le32(record + 24);
        sample.loopStart = (int64_t) le32(record + 28);
        sample.loopEnd = (int64_t) le32(record + 32);
        sample.sampleRate = (int) le32(record + 36);
        sample.originalPitch = record[40];
        sample.pitchCorrection = (int8_t) record[41];
        sample.sampleLink = le16(record + 42);
        sample.sampleType = le16(record + 44);
        if ((int64_t) sampleData.size() < sample.end) sample.end = (int64_t) sampleData.size();
        sampleList.push_back(std::move(sample));
    }
    // Instrument zones.
    const int instrumentCount = (int) (inst.size / 22) - 1;
    if (instrumentCount <= 0 || instrumentCount > maxInstruments) { error = "SoundFont instrument count is invalid"; return false; }
    std::vector<std::vector<InstrumentZone>> instruments((size_t) instrumentCount);
    int totalZones = 0;
    for (int i = 0; i < instrumentCount; ++i)
    {
        const auto* record = inst.data + (size_t) i * 22;
        const int bagIndex = le16(record + 20);
        const int nextBag = le16(record + 22 + 20);
        for (int b = bagIndex; b < nextBag; ++b)
        {
            if ((size_t) (b + 1) * 4 > ibag.size) break;
            const int start = le16(ibag.data + (size_t) b * 4);
            const int end = (size_t) (b + 2) * 4 <= ibag.size ? le16(ibag.data + (size_t) (b + 1) * 4)
                                                               : (int) (igen.size / 4);
            Sf2Zone zone;
            bool sampleSet = false, hasOverridingRoot = false;
            for (int g = start; g < end; ++g)
            {
                if ((size_t) (g + 1) * 4 > igen.size) break;
                const int oper = le16(igen.data + (size_t) g * 4);
                const juce::uint16 amount = le16(igen.data + (size_t) g * 4 + 2);
                switch (oper)
                {
                    case 43: { auto r = keyRange(amount); zone.keyLow = r.low; zone.keyHigh = r.high; } break;
                    case 44: { auto r = keyRange(amount); zone.velocityLow = r.low; zone.velocityHigh = r.high; } break;
                    case 53: zone.sampleIndex = amount; sampleSet = true; break;
                    case 54: zone.loopMode = amount & 3; break;
                    case 57: zone.exclusiveClass = amount; break;
                    case 48: zone.attenuationDb = amount * 0.1; break;
                    case 17: zone.pan = ((double) amount - 500.0) / 500.0; break;
                    case 51: zone.tuningCents += signed16(amount); break;
                    case 52: zone.tuningCents += signed16(amount) * 100.0; break;
                    case 58: zone.rootKey = amount; hasOverridingRoot = true; break;
                    default: break;
                }
            }
            if (! sampleSet || zone.sampleIndex < 0 || zone.sampleIndex >= (int) sampleList.size()) continue;
            if (! hasOverridingRoot) zone.rootKey = sampleList[(size_t) zone.sampleIndex].originalPitch;
            zone.rootKey = juce::jlimit(0, 127, zone.rootKey);
            zone.keyLow = juce::jlimit(0, 127, zone.keyLow);
            zone.keyHigh = juce::jlimit(0, 127, zone.keyHigh);
            if (zone.keyLow > zone.keyHigh) std::swap(zone.keyLow, zone.keyHigh);
            if (zone.velocityLow > zone.velocityHigh) std::swap(zone.velocityLow, zone.velocityHigh);
            instruments[(size_t) i].push_back({ zone, true });
            if (++totalZones > maxZones) { error = "SoundFont has too many zones"; return false; }
        }
    }
    // Presets: intersect preset ranges with instrument zones.
    const int presetCount = (int) (phdr.size / 38) - 1;
    if (presetCount <= 0 || presetCount > maxPresets) { error = "SoundFont preset count is invalid"; return false; }
    for (int i = 0; i < presetCount; ++i)
    {
        const auto* record = phdr.data + (size_t) i * 38;
        Sf2Preset preset;
        preset.name = readName(record, 20);
        preset.program = le16(record + 20);
        preset.bank = le16(record + 22);
        const int bagIndex = le16(record + 24);
        const int nextBag = le16(record + 38 + 24);
        for (int b = bagIndex; b < nextBag; ++b)
        {
            if ((size_t) (b + 1) * 4 > pbag.size) break;
            const int start = le16(pbag.data + (size_t) b * 4);
            const int end = (size_t) (b + 2) * 4 <= pbag.size ? le16(pbag.data + (size_t) (b + 1) * 4)
                                                               : (int) (pgen.size / 4);
            Range presetKeys, presetVelocity;
            int instrumentIndex = -1;
            for (int g = start; g < end; ++g)
            {
                if ((size_t) (g + 1) * 4 > pgen.size) break;
                const int oper = le16(pgen.data + (size_t) g * 4);
                const juce::uint16 amount = le16(pgen.data + (size_t) g * 4 + 2);
                if (oper == 41) instrumentIndex = amount;
                else if (oper == 43) { auto r = keyRange(amount); presetKeys = r; }
                else if (oper == 44) { auto r = keyRange(amount); presetVelocity = r; }
            }
            if (instrumentIndex < 0 || instrumentIndex >= (int) instruments.size()) continue;
            for (const auto& instrumentZone : instruments[(size_t) instrumentIndex])
            {
                auto zone = instrumentZone.zone;
                zone.keyLow = juce::jmax(zone.keyLow, presetKeys.low);
                zone.keyHigh = juce::jmin(zone.keyHigh, presetKeys.high);
                zone.velocityLow = juce::jmax(zone.velocityLow, presetVelocity.low);
                zone.velocityHigh = juce::jmin(zone.velocityHigh, presetVelocity.high);
                if (zone.keyLow > zone.keyHigh || zone.velocityLow > zone.velocityHigh) continue;
                preset.zones.push_back(zone);
                if (preset.zones.size() > (size_t) maxZones) break;
            }
        }
        if (! preset.zones.empty()) presetList.push_back(std::move(preset));
    }
    if (presetList.empty()) { error = "SoundFont has no playable presets"; return false; }
    loaded = true;
    return true;
}
std::vector<SampleDescriptor> Sf2Bank::usedSamples(int presetIndex, juce::String& error) const
{
    error.clear();
    std::vector<SampleDescriptor> result;
    if (! loaded) { error = "SoundFont is not loaded"; return result; }
    if (presetIndex < 0 || presetIndex >= (int) presetList.size())
    {
        error = "SoundFont preset index is out of range";
        return result;
    }
    for (const auto& zone : presetList[(size_t) presetIndex].zones)
    {
        if (zone.sampleIndex < 0) continue;
        const auto resolved = resolveSample(sampleList, sampleData, zone.sampleIndex);
        const int canonical = resolved.left >= 0 ? resolved.left : zone.sampleIndex;
        auto found = std::find_if(result.begin(), result.end(),
            [canonical] (const SampleDescriptor& d) { return d.id == canonical; });
        if (found == result.end())
        {
            SampleDescriptor descriptor;
            descriptor.id = canonical;
            if (canonical >= 0 && canonical < (int) sampleList.size())
                descriptor.name = sampleList[(size_t) canonical].name;
            descriptor.error = resolved.error;
            if (resolved.error.isEmpty())
            {
                descriptor.sampleRate = resolved.rate;
                descriptor.channels = resolved.channels;
                descriptor.frameCount = resolved.frames;
                descriptor.loopStart = resolved.loopStart;
                descriptor.loopEnd = resolved.loopEnd;
                descriptor.looping = resolved.looping;
            }
            result.push_back(std::move(descriptor));
            found = result.end() - 1;
        }
        found->zones.push_back({ zone.keyLow, zone.keyHigh, zone.velocityLow, zone.velocityHigh });
    }
    return result;
}
SampleAudioPtr Sf2Bank::sampleAudio(SampleId id, juce::String& error) const
{
    error.clear();
    if (! loaded) { error = "SoundFont is not loaded"; return {}; }
    const auto resolved = resolveSample(sampleList, sampleData, id);
    if (! resolved.error.isEmpty()) { error = resolved.error; return {}; }
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = resolved.rate;
    audio->channels = resolved.channels;
    audio->bitsPerSample = 16;
    audio->floatingPoint = false;
    audio->frames = resolved.frames;
    audio->left.resize((size_t) resolved.frames);
    for (int64_t i = 0; i < resolved.frames; ++i)
        audio->left[(size_t) i] = (float) sampleData[(size_t) (resolved.start + i)] / 32768.0f;
    if (resolved.channels > 1)
    {
        audio->right.resize((size_t) resolved.frames);
        const int64_t rightStart = sampleList[(size_t) resolved.right].start;
        for (int64_t i = 0; i < resolved.frames; ++i)
            audio->right[(size_t) i] = (float) sampleData[(size_t) (rightStart + i)] / 32768.0f;
    }
    return audio;
}
} // namespace sonicator
