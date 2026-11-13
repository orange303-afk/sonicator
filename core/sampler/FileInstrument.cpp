#include "FileInstrument.h"
#include "core/audio/SonicatorFormats.h"
#include "core/kurzweil/KrzInstrument.h"
#include "core/sfz/SfzInstrument.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <zlib.h>
namespace sonicator {
namespace {
constexpr int64_t maxInstrumentFileSize = 64 * 1024 * 1024;
constexpr int64_t maxDecodedBytes = 256 * 1024 * 1024;
constexpr int maxSources = 4096;
constexpr int maxZones = 65536;
constexpr int maxNestedFiles = 20000;
constexpr int maxSearchDepth = 4;
struct ParsedZone
{
    ExternalZone zone;
    int sampleRef = -1;
};
struct ParsedInstrument
{
    juce::String name;
    std::vector<juce::String> sampleDirectories, sampleNames;
    std::vector<ParsedZone> zones;
    juce::StringArray warnings;
};
juce::uint16 le16(const juce::uint8* p) { return (juce::uint16) (p[0] | (p[1] << 8)); }
juce::uint32 le32(const juce::uint8* p)
{
    return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8)
         | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
}
juce::String readFixedString(const juce::uint8* data, int maxLength)
{
    int length = 0;
    while (length < maxLength && data[length] != 0) ++length;
    return juce::String::fromUTF8((const char*) data, length).trim();
}
juce::String normaliseSeparators(juce::String value)
{
    value = value.replaceCharacter('\\', '/').replaceCharacter(':', '/');
    while (value.contains("//")) value = value.replace("//", "/");
    return value;
}
bool decodeZlib(const juce::uint8* data, size_t size, std::vector<juce::uint8>& output)
{
    for (size_t start = 0; start + 2 < size && start < 1024; ++start)
    {
        const auto second = data[start + 1];
        if (data[start] != 0x78 || (second != 0x01 && second != 0x9c && second != 0xda)) continue;
        z_stream stream {};
        stream.next_in = const_cast<Bytef*>(data + start);
        stream.avail_in = (uInt) juce::jmin<size_t>(size - start, 0x7fffffffu);
        if (inflateInit(&stream) != Z_OK) continue;
        std::vector<juce::uint8> buffer(1 << 20);
        int status = Z_OK;
        int64_t total = 0;
        while (status == Z_OK)
        {
            stream.next_out = buffer.data();
            stream.avail_out = (uInt) buffer.size();
            status = inflate(&stream, Z_NO_FLUSH);
            const auto produced = (int64_t) buffer.size() - stream.avail_out;
            total += produced;
            if (total > maxInstrumentFileSize) { status = Z_MEM_ERROR; break; }
            output.insert(output.end(), buffer.begin(), buffer.begin() + produced);
        }
        inflateEnd(&stream);
        if (status == Z_STREAM_END) return true;
        output.clear();
    }
    return false;
}
// Resolves a stored sample path without writing anything. Tries an absolute path, the stored
// relative path, suffixes of the stored path under the instrument folder and its parents, then a
// bounded basename search below the instrument folder. Two basename matches are an error.
juce::File resolveSample(const juce::File& instrumentFile, const juce::String& storedDirectory,
                         const juce::String& fileName, juce::String& error)
{
    const auto directory = normaliseSeparators(storedDirectory).trimCharactersAtEnd("/");
    juce::StringArray segments;
    segments.addTokens(directory, "/", "");
    segments.removeEmptyStrings();
    auto instrumentDir = instrumentFile.getParentDirectory();
    auto check = [&] (const juce::File& candidate) { return candidate.existsAsFile() ? candidate : juce::File(); };
    if (directory.startsWith("/"))
    {
        if (auto found = check(juce::File(directory + "/" + fileName)); found != juce::File()) return found;
    }
    if (directory.isNotEmpty())
        if (auto found = check(instrumentDir.getChildFile(directory).getChildFile(fileName)); found != juce::File())
            return found;
    auto ancestor = instrumentDir;
    for (int depth = 0; depth < 5 && ancestor != juce::File(); ++depth)
    {
        for (int first = 0; first < segments.size(); ++first)
        {
            juce::File candidate = ancestor;
            for (int i = first; i < segments.size(); ++i) candidate = candidate.getChildFile(segments[i]);
            candidate = candidate.getChildFile(fileName);
            if (auto found = check(candidate); found != juce::File()) return found;
        }
        if (auto found = check(ancestor.getChildFile(fileName)); found != juce::File()) return found;
        const auto parent = ancestor.getParentDirectory();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    std::vector<juce::File> matches;
    int visited = 0;
    std::function<void(const juce::File&, int)> search = [&](const juce::File& folder, int depth)
    {
        if (depth > maxSearchDepth || visited > maxNestedFiles) return;
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*",
                                                               juce::File::findFilesAndDirectories))
        {
            if (++visited > maxNestedFiles) return;
            const auto child = entry.getFile();
            if (child.isDirectory()) search(child, depth + 1);
            else if (child.getFileName().equalsIgnoreCase(fileName)) matches.push_back(child);
        }
    };
    search(instrumentDir, 0);
    if (matches.size() == 1) return matches.front();
    if (matches.size() > 1) { error = "Ambiguous sample name: " + fileName; return {}; }
    error = "Cannot resolve sample: " + fileName;
    return {};
}
SampleAudioPtr decodeAudio(const juce::File& file, juce::AudioFormatManager& formats,
                           int64_t& decodedBytes, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) { error = "Cannot decode sample: " + file.getFileName(); return {}; }
    if (! (reader->sampleRate > 0) || ! std::isfinite(reader->sampleRate))
    { error = "Sample has an invalid rate: " + file.getFileName(); return {}; }
    if (reader->numChannels < 1 || reader->numChannels > 2)
    { error = "Sample must be mono or stereo: " + file.getFileName(); return {}; }
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > (int64_t) INT_MAX)
    { error = "Sample has no usable frames: " + file.getFileName(); return {}; }
    const auto bytes = (int64_t) reader->lengthInSamples * (int64_t) reader->numChannels * 4;
    if (decodedBytes + bytes > maxDecodedBytes)
    { error = "Instrument exceeds the 256 MiB decoded sample limit"; return {}; }
    juce::AudioBuffer<float> buffer((int) reader->numChannels, (int) reader->lengthInSamples);
    if (! reader->read(&buffer, 0, (int) reader->lengthInSamples, 0, true, true))
    { error = "Cannot read sample: " + file.getFileName(); return {}; }
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = (int) std::lround(reader->sampleRate);
    audio->channels = (int) reader->numChannels;
    audio->bitsPerSample = reader->bitsPerSample;
    audio->floatingPoint = reader->usesFloatingPointData;
    audio->frames = reader->lengthInSamples;
    audio->left.resize((size_t) audio->frames);
    for (int64_t i = 0; i < audio->frames; ++i)
    {
        const float value = buffer.getSample(0, (int) i);
        audio->left[(size_t) i] = std::isfinite(value) ? value : 0.0f;
    }
    if (audio->channels > 1)
    {
        audio->right.resize((size_t) audio->frames);
        for (int64_t i = 0; i < audio->frames; ++i)
        {
            const float value = buffer.getSample(1, (int) i);
            audio->right[(size_t) i] = std::isfinite(value) ? value : 0.0f;
        }
    }
    decodedBytes += bytes;
    return audio;
}
bool readExs(const juce::File& file, ParsedInstrument& parsed, juce::String& error)
{
    if (file.getSize() <= 84 || file.getSize() > maxInstrumentFileSize)
    { error = "EXS24 file has an invalid size"; return false; }
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block)) { error = "Cannot read EXS24 file"; return false; }
    const auto* bytes = (const juce::uint8*) block.getData();
    const auto size = (size_t) block.getSize();
    if (bytes[0] != 1 || bytes[1] != 1 || std::memcmp(bytes + 16, "TBOS", 4) != 0)
    { error = "Not a supported little-endian EXS24 instrument"; return false; }
    std::map<int, int> sampleByIndex;
    ParsedInstrument instrument;
    size_t position = 0;
    int blockCount = 0;
    while (position + 84 <= size)
    {
        if (++blockCount > 100000) { error = "EXS24 has too many blocks"; return false; }
        const auto* header = bytes + position;
        const auto payloadSize = (size_t) (header[4] | ((header[5] & 0x7f) << 8));
        if (payloadSize > size - position - 84) { error = "EXS24 block exceeds the file"; return false; }
        const auto marker = header[3] & 0x0f;
        const auto index = (int) le32(header + 8);
        const auto* payload = header + 84;
        const auto name = readFixedString(header + 20, 64);
        if (marker == 0)
        {
            if (instrument.name.isEmpty() && name.isNotEmpty() && name != "(null)")
                instrument.name = name;
        }
        else if (marker == 1)
        {
            if (payloadSize < 96) { error = "EXS24 zone block is truncated"; return false; }
            ParsedZone pz;
            auto& zone = pz.zone;
            const auto flags = payload[0];
            const bool oneshot = (flags & 1) != 0;
            if ((flags & 4) != 0) instrument.warnings.add("EXS24 reverse zone plays forward");
            zone.keyLow = payload[6]; zone.keyHigh = payload[7];
            zone.velocityLow = payload[9]; zone.velocityHigh = payload[10];
            zone.rootKey = payload[1];
            zone.tuningCents = (double) (int8_t) payload[2];
            zone.pan = (double) (int8_t) payload[3] / 64.0;
            zone.attenuationDb = -(double) (int8_t) payload[4];
            zone.end = (int64_t) le32(payload + 16);
            zone.start = (int64_t) le32(payload + 20);
            zone.loopEnd = (int64_t) le32(payload + 24);
            zone.loopStart = (int64_t) le32(payload + 28);
            // Byte 33 carries the loop options; bit 0 is loop-on in the files checked here.
            const bool loopOn = payloadSize > 33 && (payload[33] & 1) != 0;
            zone.looping = loopOn && zone.loopEnd > zone.loopStart;
            zone.oneShot = oneshot;
            // Older 96-byte zones end with the sample index; 104+ add a group index first.
            pz.sampleRef = (int) le32(payload + (payloadSize >= 100 ? 92 : 88));
            if (zone.keyLow > zone.keyHigh) std::swap(zone.keyLow, zone.keyHigh);
            if (zone.velocityLow > zone.velocityHigh) std::swap(zone.velocityLow, zone.velocityHigh);
            instrument.zones.push_back(pz);
        }
        else if (marker == 3)
        {
            if (payloadSize < 336) { error = "EXS24 sample block is truncated"; return false; }
            const auto fileName = name.isNotEmpty() ? name
                : (payloadSize >= 592 ? readFixedString(payload + 336, 256) : juce::String());
            instrument.sampleDirectories.push_back(readFixedString(payload + 80, 256));
            instrument.sampleNames.push_back(fileName);
            // EXS sample references use the sample-block order. Some writers leave the
            // block header index at zero, so the header index is not a usable key.
            sampleByIndex[(int) instrument.sampleNames.size() - 1] = (int) instrument.sampleNames.size() - 1;
        }
        position += 84 + payloadSize;
    }
    if (instrument.name.isEmpty()) instrument.name = file.getFileNameWithoutExtension();
    if (instrument.zones.empty()) { error = "EXS24 instrument has no zones"; return false; }
    for (auto& zone : instrument.zones)
    {
        const auto found = sampleByIndex.find(zone.sampleRef);
        if (found == sampleByIndex.end())
        {
            instrument.warnings.add("EXS24 zone references a missing sample block");
            zone.sampleRef = -1;
        }
        else zone.sampleRef = found->second;
    }
    parsed = std::move(instrument);
    return true;
}
juce::String decodeNkiPath(const juce::String& raw)
{
    if (! raw.startsWith("@")) return normaliseSeparators(raw);
    juce::String out;
    int i = 1;
    while (i < raw.length())
    {
        const auto c = raw[i];
        if (c == 'b') { out << "../"; ++i; }
        else if (c == 'd' && i + 4 < raw.length())
        {
            const int available = raw.length() - i - 4;
            const int length = juce::jlimit(0, available, raw.substring(i + 1, i + 4).getIntValue());
            out << raw.substring(i + 4, i + 4 + length) << "/";
            i += 4 + length;
        }
        else if (c == 'F' && i + 12 < raw.length())
        {
            const auto code = raw.substring(i + 2, i + 12);
            const int available = raw.length() - i - 12;
            int length = code.substring(4, 7).getIntValue();
            if (length <= 0) length = available;
            length = juce::jlimit(0, available, length);
            out << raw.substring(i + 12, i + 12 + length);
            i += 12 + length;
        }
        else { out << c; ++i; }
    }
    return out;
}
bool readNki(const juce::File& file, ParsedInstrument& parsed, juce::String& error)
{
    if (file.getSize() <= 16 || file.getSize() > maxInstrumentFileSize)
    { error = "NKI file has an invalid size"; return false; }
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block)) { error = "Cannot read NKI file"; return false; }
    const auto* bytes = (const juce::uint8*) block.getData();
    const auto size = (size_t) block.getSize();
    const auto magic = le32(bytes);
    if (magic == 0x4e205c2fu)
    { error = "NI file container NKI is unsupported by the built-in reader"; return false; }
    std::vector<juce::uint8> xml;
    if (! decodeZlib(bytes, size, xml))
    { error = "Unsupported NKI container (no readable XML payload)"; return false; }
    auto document = juce::XmlDocument::parse(juce::String::fromUTF8((const char*) xml.data(), (int) xml.size()));
    if (document == nullptr) { error = "NKI XML payload is malformed"; return false; }
    const auto rootName = document->getTagName();
    const bool k2 = rootName == "K2_Container";
    const bool niss = rootName == "NiSS_Program";
    if (! k2 && ! niss) { error = "Unsupported NKI XML root: " + rootName; return false; }
    ParsedInstrument instrument;
    instrument.name = document->getStringAttribute("name", file.getFileNameWithoutExtension());
    if (k2)
        if (const auto* programs = document->getChildByName("Programs"))
            if (const auto* program = programs->getChildByName("K2_Program"))
            {
                const auto programName = program->getStringAttribute("name");
                if (programName.isNotEmpty() && (instrument.name.isEmpty() || instrument.name == "(null)"))
                    instrument.name = programName;
            }
    int zoneCount = 0;
    std::function<void(const juce::XmlElement&)> visit = [&](const juce::XmlElement& element)
    {
        const bool isZone = element.hasTagName("K2_Zone") || element.hasTagName("NiSS_Zone");
        if (isZone)
        {
            if (++zoneCount > maxZones) return;
            ParsedZone pz;
            auto& zone = pz.zone;
            std::map<juce::String, juce::String> values;
            if (const auto* params = element.getChildByName("Parameters"))
                for (int i = 0; i < params->getNumChildElements(); ++i)
                {
                    const auto* v = params->getChildElement(i);
                    if (v != nullptr && v->hasTagName("V"))
                        values[v->getStringAttribute("name")] = v->getStringAttribute("value");
                }
            auto number = [&] (const juce::String& key, int fallback)
            { return values.count(key) ? values[key].getIntValue() : fallback; };
            zone.keyLow = juce::jlimit(0, 127, number("lowKey", 0));
            zone.keyHigh = juce::jlimit(0, 127, number("highKey", 127));
            zone.velocityLow = juce::jlimit(0, 127, number("lowVelocity", 1));
            zone.velocityHigh = juce::jlimit(0, 127, number("highVelocity", 127));
            zone.rootKey = juce::jlimit(0, 127, number("rootKey", 60));
            // Drum-map NKI zones are single-key and one-shot; let them ring out on key release.
            zone.oneShot = zone.keyLow == zone.keyHigh;
            zone.start = number("sampleStart", 0);
            zone.end = number("sampleEnd", 0);
            const double tune = values.count("zoneTune") ? values["zoneTune"].getDoubleValue() : 1.0;
            if (tune > 0 && std::abs(tune - 1.0) > 1e-9)
                zone.tuningCents = 1200.0 * std::log2(tune);
            if (const auto* loops = element.getChildByName("Loops"); loops != nullptr
                && loops->getNumChildElements() > 0)
                instrument.warnings.add("NKI loop points are not read yet; the zone plays one-shot");
            if (const auto* sample = element.getChildByName("Sample"))
            {
                juce::String fileName;
                for (int i = 0; i < sample->getNumChildElements(); ++i)
                {
                    const auto* v = sample->getChildElement(i);
                    if (v == nullptr || ! v->hasTagName("V")) continue;
                    if (v->getStringAttribute("name") == "file_ex2")
                        fileName = decodeNkiPath(v->getStringAttribute("value"));
                    else if (v->getStringAttribute("name") == "file")
                        fileName = normaliseSeparators(v->getStringAttribute("value"));
                }
                if (fileName.isNotEmpty())
                {
                    const auto slash = fileName.lastIndexOfChar('/');
                    instrument.sampleDirectories.push_back(slash >= 0 ? fileName.substring(0, slash) : juce::String());
                    instrument.sampleNames.push_back(slash >= 0 ? fileName.substring(slash + 1) : fileName);
                    pz.sampleRef = (int) instrument.sampleNames.size() - 1;
                }
            }
            instrument.zones.push_back(pz);
        }
        for (int i = 0; i < element.getNumChildElements(); ++i) visit(*element.getChildElement(i));
    };
    visit(*document);
    if (instrument.zones.empty()) { error = "NKI instrument has no zones"; return false; }
    parsed = std::move(instrument);
    return true;
}
} // namespace
bool isExs24File(const juce::File& file)
{
    if (! file.existsAsFile() || file.getSize() < 84) return false;
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) return false;
    juce::uint8 header[20] {};
    if (stream.read(header, 20) != 20) return false;
    return header[0] == 1 && header[1] == 1
        && (std::memcmp(header + 16, "TBOS", 4) == 0 || std::memcmp(header + 16, "JBOS", 4) == 0);
}
bool isNkiFile(const juce::File& file, juce::String& detail)
{
    detail.clear();
    if (! file.existsAsFile() || file.getSize() < 16) return false;
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) return false;
    juce::uint8 header[4] {};
    if (stream.read(header, 4) != 4) return false;
    const auto magic = le32(header);
    if (magic == 0x7fa89012u || magic == 0xb36ee55eu)
    { detail = "NKI XML container"; return true; }
    if (magic == 0x4e205c2fu)
    { detail = "NI file container is unsupported"; return true; }
    return false;
}
bool loadFileInstrument(const juce::File& file, FileInstrumentFormat format,
                        FileInstrument& instrument, juce::String& error)
{
    if (format == FileInstrumentFormat::krz) return loadKrzInstrument(file, instrument, error);
    if (format == FileInstrumentFormat::sfz) return loadSfzInstrument(file, instrument, error);
    error.clear();
    instrument = {};
    instrument.file = file;
    ParsedInstrument parsed;
    const bool ok = format == FileInstrumentFormat::exs24 ? readExs(file, parsed, error)
                                                          : readNki(file, parsed, error);
    if (! ok) return false;
    instrument.name = parsed.name;
    instrument.warnings = parsed.warnings;
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    int64_t decodedBytes = 0;
    std::map<juce::String, int> sourceByPath;
    std::vector<int> refToSource(parsed.sampleNames.size(), -1);
    for (int ref = 0; ref < (int) parsed.sampleNames.size(); ++ref)
    {
        juce::String resolveError;
        auto resolved = resolveSample(file, parsed.sampleDirectories[(size_t) ref],
                                      parsed.sampleNames[(size_t) ref], resolveError);
        if (resolved == juce::File())
        {
            SampleDescriptor descriptor;
            descriptor.id = (int) instrument.sources.size();
            descriptor.name = parsed.sampleNames[(size_t) ref];
            descriptor.error = resolveError;
            instrument.sources.push_back(std::move(descriptor));
            instrument.audio.push_back({});
            refToSource[(size_t) ref] = descriptor.id;
            instrument.partial = true;
            instrument.warnings.add(resolveError);
            continue;
        }
        const auto key = resolved.getFullPathName();
        const auto existing = sourceByPath.find(key);
        if (existing != sourceByPath.end()) { refToSource[(size_t) ref] = existing->second; continue; }
        if ((int) instrument.sources.size() >= maxSources)
        {
            error = "Instrument has too many samples";
            return false;
        }
        juce::String decodeError;
        auto audio = decodeAudio(resolved, formats, decodedBytes, decodeError);
        SampleDescriptor descriptor;
        descriptor.id = (int) instrument.sources.size();
        descriptor.name = parsed.sampleNames[(size_t) ref].isNotEmpty()
                              ? parsed.sampleNames[(size_t) ref] : resolved.getFileName();
        if (audio == nullptr)
        {
            descriptor.error = decodeError;
            instrument.partial = true;
            instrument.warnings.add(decodeError);
        }
        else
        {
            descriptor.sampleRate = audio->sampleRate;
            descriptor.channels = audio->channels;
            descriptor.frameCount = audio->frames;
        }
        sourceByPath[key] = descriptor.id;
        refToSource[(size_t) ref] = descriptor.id;
        instrument.sources.push_back(std::move(descriptor));
        instrument.audio.push_back(std::move(audio));
    }
    for (const auto& parsedZone : parsed.zones)
    {
        if (parsedZone.sampleRef < 0 || parsedZone.sampleRef >= (int) refToSource.size()) continue;
        const int source = refToSource[(size_t) parsedZone.sampleRef];
        if (source < 0 || source >= (int) instrument.audio.size() || instrument.audio[(size_t) source] == nullptr)
            continue;
        const auto& audio = *instrument.audio[(size_t) source];
        auto zone = parsedZone.zone;
        if (zone.end <= zone.start) zone.end = audio.frames;
        zone.start = juce::jlimit<int64_t>(0, audio.frames, zone.start);
        zone.end = juce::jlimit<int64_t>(0, audio.frames, zone.end);
        if (zone.end <= zone.start)
        {
            instrument.warnings.add("Sample zone is shorter than one frame");
            continue;
        }
        if (zone.looping)
        {
            zone.loopStart = juce::jlimit(zone.start, zone.end, zone.loopStart);
            zone.loopEnd = juce::jlimit(zone.start, zone.end, zone.loopEnd);
            zone.looping = zone.loopEnd > zone.loopStart;
        }
        zone.sampleIndex = source;
        auto& descriptor = instrument.sources[(size_t) source];
        descriptor.zones.push_back({ zone.keyLow, zone.keyHigh, zone.velocityLow, zone.velocityHigh });
        if (! descriptor.looping && zone.looping)
        {
            descriptor.looping = true;
            descriptor.loopStart = zone.loopStart - zone.start;
            descriptor.loopEnd = zone.loopEnd - zone.start;
        }
        instrument.zones.push_back(zone);
    }
    if (instrument.zones.empty())
    {
        error = instrument.warnings.isEmpty() ? "Instrument has no playable zones"
                                              : instrument.warnings.joinIntoString("; ");
        return false;
    }
    return true;
}
} // namespace sonicator
