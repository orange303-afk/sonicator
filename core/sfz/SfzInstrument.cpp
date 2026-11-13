#include "SfzInstrument.h"
#include "core/audio/SonicatorFormats.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>
namespace sonicator {
namespace {
constexpr int64_t maxInstrumentFileSize = 64 * 1024 * 1024;
constexpr int64_t maxDecodedBytes = 256 * 1024 * 1024;
constexpr int maxSources = 4096;
constexpr int maxZones = 65536;
constexpr int maxNestedFiles = 20000;
constexpr int maxSearchDepth = 4;

using OpcodeMap = std::map<juce::String, juce::String>;

juce::String normaliseSeparators(juce::String value)
{
    value = value.replaceCharacter('\\', '/').replaceCharacter(':', '/');
    while (value.contains("//")) value = value.replace("//", "/");
    return value;
}

juce::String stripComments(const juce::String& line)
{
    bool quoted = false;
    juce::juce_wchar quote = 0;
    for (int i = 0; i + 1 < line.length(); ++i)
    {
        const auto c = line[i];
        if (quoted)
        {
            if (c == quote) quoted = false;
            continue;
        }
        if (c == '"' || c == '\'') { quoted = true; quote = c; continue; }
        if (c == '/' && line[i + 1] == '/') return line.substring(0, i);
    }
    return line;
}

// Splits one SFZ line into `key=value` tokens. Quoted values may contain spaces. A token that is
// not a section header and has no `=` is ignored (the format has a few bare words in the wild).
void tokenise(const juce::String& rawLine,
              std::vector<std::pair<juce::String, juce::String>>& tokens,
              juce::String& section, bool& headerSeen)
{
    headerSeen = false;
    const auto line = stripComments(rawLine).trim();
    int i = 0;
    auto skipSpace = [&]
    {
        while (i < line.length() && juce::CharacterFunctions::isWhitespace(line[i])) ++i;
    };
    skipSpace();
    while (i < line.length())
    {
        if (line[i] == '<')
        {
            const int close = line.indexOfChar(i + 1, '>');
            if (close < 0) { section = line.substring(i + 1).trim().toLowerCase(); i = line.length(); break; }
            section = line.substring(i + 1, close).trim().toLowerCase();
            headerSeen = true;
            i = close + 1;
            skipSpace();
            continue;
        }
        const int keyStart = i;
        while (i < line.length() && line[i] != '='
               && ! juce::CharacterFunctions::isWhitespace(line[i])) ++i;
        const auto key = line.substring(keyStart, i).trim().toLowerCase();
        skipSpace();
        if (i < line.length() && line[i] == '=')
        {
            ++i;
            juce::String value;
            if (i < line.length() && (line[i] == '"' || line[i] == '\''))
            {
                const auto quote = line[i++];
                const int end = line.indexOfChar(i, quote);
                if (end < 0) { value = line.substring(i); i = line.length(); }
                else { value = line.substring(i, end); i = end + 1; }
            }
            else
            {
                // Chicken Systems and several old editors write unquoted sample paths with
                // spaces. A space only ends the value when the following token is another
                // `opcode=value` pair, so `..\..\One Shots\Kick.wav lokey=36` stays intact.
                const int valueStart = i;
                while (i < line.length())
                {
                    if (! juce::CharacterFunctions::isWhitespace(line[i])) { ++i; continue; }
                    int look = i;
                    while (look < line.length()
                           && juce::CharacterFunctions::isWhitespace(line[look])) ++look;
                    int end = look;
                    while (end < line.length()
                           && ! juce::CharacterFunctions::isWhitespace(line[end])
                           && line[end] != '=')
                        ++end;
                    if (end < line.length() && line[end] == '=' && end > look) break;
                    i = look;
                }
                value = line.substring(valueStart, i).trim();
            }
            if (key.isNotEmpty()) tokens.push_back({ key, value });
        }
        else if (i < line.length()) ++i;
        skipSpace();
    }
}

juce::String substituteVariables(juce::String value, const OpcodeMap& variables)
{
    for (int pass = 0; pass < 4; ++pass)
    {
        bool changed = false;
        for (const auto& [name, replacement] : variables)
        {
            const auto dollar = "$" + name;
            if (value.contains(dollar)) { value = value.replace(dollar, replacement); changed = true; }
            const auto braced = "${" + name + "}";
            if (value.contains(braced)) { value = value.replace(braced, replacement); changed = true; }
        }
        if (! changed) break;
    }
    return value;
}

int noteNumber(juce::String text, int fallback)
{
    text = text.trim();
    if (text.isEmpty()) return fallback;
    if (juce::CharacterFunctions::isDigit(text[0]) || text[0] == '-' || text[0] == '+')
        return juce::jlimit(0, 127, text.getIntValue());
    int semitone = -1;
    switch (juce::CharacterFunctions::toUpperCase(text[0]))
    {
        case 'C': semitone = 0; break;
        case 'D': semitone = 2; break;
        case 'E': semitone = 4; break;
        case 'F': semitone = 5; break;
        case 'G': semitone = 7; break;
        case 'A': semitone = 9; break;
        case 'B': semitone = 11; break;
        default: return fallback;
    }
    int position = 1;
    if (position < text.length() && text[position] == '#') { ++semitone; ++position; }
    else if (position < text.length() && (text[position] == 'b' || text[position] == 'B'))
    { --semitone; ++position; }
    const int octave = text.substring(position).getIntValue();
    return juce::jlimit(0, 127, semitone + 12 * (octave + 1));
}

int intOpcode(const OpcodeMap& map, const juce::String& key, int fallback)
{
    const auto found = map.find(key);
    return found == map.end() ? fallback : found->second.trim().getIntValue();
}

double doubleOpcode(const OpcodeMap& map, const juce::String& key, double fallback)
{
    const auto found = map.find(key);
    return found == map.end() ? fallback : found->second.trim().getDoubleValue();
}

juce::String stringOpcode(const OpcodeMap& map, const juce::String& key)
{
    const auto found = map.find(key);
    return found == map.end() ? juce::String() : found->second.trim();
}

juce::File resolveSample(const juce::File& instrumentFile, const juce::String& defaultPath,
                         const juce::String& stored, juce::String& error)
{
    auto sample = normaliseSeparators(stored).trim();
    if (sample.isEmpty()) { error = "SFZ region has no sample"; return {}; }
    if (sample.startsWith("*"))
    {
        error = "SFZ built-in sample is not supported: " + stored;
        return {};
    }
    while (sample.startsWithChar('/')) sample = sample.substring(1);
    const auto base = instrumentFile.getParentDirectory();
    auto path = normaliseSeparators(defaultPath).trim();
    while (path.startsWithChar('/')) path = path.substring(1);
    std::vector<juce::File> candidates;
    auto add = [&candidates] (const juce::File& file)
    {
        if (std::find(candidates.begin(), candidates.end(), file) == candidates.end())
            candidates.push_back(file);
    };
    if (path.isNotEmpty() && path.containsChar(':')) add(juce::File(path).getChildFile(sample));
    // Sibling packages often put the programs in one folder and the samples in another
    // (`Programs/.../Kit.sfz` with `default_path` under a sibling `Multisamples/`). Walk up.
    auto ancestor = base;
    std::vector<juce::File> ancestors;
    for (int depth = 0; depth < 6 && ancestor != juce::File(); ++depth)
    {
        ancestors.push_back(ancestor);
        if (path.isNotEmpty()) add(ancestor.getChildFile(path).getChildFile(sample));
        add(ancestor.getChildFile(sample));
        const auto parent = ancestor.getParentDirectory();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    for (const auto& candidate : candidates)
        if (candidate.existsAsFile()) return candidate;

    // Absolute paths from another machine and flat folders: a bounded basename search. Only one
    // match is accepted, so a duplicated name is an explicit ambiguity, never a guess.
    const auto fileName = juce::File(sample).getFileName();
    for (const auto& folder : ancestors)
    {
        std::vector<juce::File> matches;
        int visited = 0;
        std::function<void(const juce::File&, int)> search = [&](const juce::File& current, int depth)
        {
            if (depth > maxSearchDepth || visited > maxNestedFiles) return;
            for (const auto& entry : juce::RangedDirectoryIterator(current, false, "*",
                                                                   juce::File::findFilesAndDirectories))
            {
                if (++visited > maxNestedFiles) return;
                const auto child = entry.getFile();
                if (child.isDirectory()) search(child, depth + 1);
                else if (child.getFileName().equalsIgnoreCase(fileName)) matches.push_back(child);
            }
        };
        if (fileName.isNotEmpty()) search(folder, 0);
        if (matches.size() == 1) return matches.front();
        if (matches.size() > 1)
        {
            error = "Ambiguous SFZ sample name: " + stored;
            return {};
        }
    }
    error = "Cannot resolve SFZ sample: " + stored;
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
} // namespace

bool isSfzFile(const juce::File& file, juce::String* detail)
{
    if (detail != nullptr) detail->clear();
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > maxInstrumentFileSize)
    {
        if (detail != nullptr) *detail = "SFZ file has an invalid size";
        return false;
    }
    juce::FileInputStream stream(file);
    if (! stream.openedOk())
    {
        if (detail != nullptr) *detail = "Cannot read the SFZ file";
        return false;
    }
    const auto bytes = (int) juce::jmin<int64_t>(file.getSize(), 64 * 1024);
    juce::MemoryBlock block((size_t) bytes);
    const int read = stream.read(block.getData(), bytes);
    if (read <= 0)
    {
        if (detail != nullptr) *detail = "Cannot read the SFZ file";
        return false;
    }
    const auto text = juce::String::fromUTF8((const char*) block.getData(), read).toLowerCase();
    if (text.contains("<region") || text.contains("<group") || text.contains("<control")
        || text.contains("<master") || text.contains("<global"))
        return true;
    if (detail != nullptr) *detail = "Not a recognised SFZ instrument";
    return false;
}

bool loadSfzInstrument(const juce::File& file, FileInstrument& instrument, juce::String& error)
{
    error.clear();
    instrument = {};
    instrument.file = file;
    instrument.name = file.getFileNameWithoutExtension();
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > maxInstrumentFileSize)
    {
        error = "SFZ file has an invalid size";
        return false;
    }
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block))
    {
        error = "Cannot read the SFZ file";
        return false;
    }
    const auto text = juce::String::fromUTF8((const char*) block.getData(), (int) block.getSize());
    if (text.isEmpty() && block.getSize() > 0)
    {
        error = "SFZ file is not readable text";
        return false;
    }

    OpcodeMap master, group, variables;
    std::vector<OpcodeMap> regions;
    OpcodeMap pending;
    juce::String section;
    auto flushRegion = [&]
    {
        if (! pending.empty()) regions.push_back(pending);
        pending.clear();
    };
    for (const auto& rawLine : juce::StringArray::fromLines(text))
    {
        std::vector<std::pair<juce::String, juce::String>> tokens;
        juce::String nextSection = section;
        bool headerSeen = false;
        tokenise(rawLine, tokens, nextSection, headerSeen);
        if (headerSeen)
        {
            flushRegion();
            section = nextSection;
            if (section == "control" || section == "global" || section == "master"
                || section == "group")
                group.clear();
        }
        for (auto& [key, value] : tokens)
        {
            value = substituteVariables(value, variables);
            if (key.startsWithChar('$'))
            {
                variables[key.substring(1)] = value;
                continue;
            }
            if (section == "control" || section == "global" || section == "master")
                master[key] = value;
            else if (section == "group")
                group[key] = value;
            else if (section == "region")
                pending[key] = value;
            // Other sections (<curve>, <effect>, <midi>) carry no sample mapping.
        }
    }
    flushRegion();
    if (regions.empty())
    {
        error = "SFZ instrument has no <region> definitions";
        return false;
    }
    if ((int) regions.size() > maxZones)
    {
        error = "SFZ instrument has too many regions";
        return false;
    }

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    int64_t decodedBytes = 0;
    std::map<juce::String, int> sourceByPath;
    std::map<juce::String, juce::File> resolvedByKey;
    std::map<juce::String, juce::String> resolveErrors;
    std::map<juce::String, int> missingSource;
    for (auto& region : regions)
    {
        OpcodeMap merged = master;
        for (const auto& [key, value] : group) merged[key] = value;
        for (const auto& [key, value] : region) merged[key] = value;
        const auto sample = stringOpcode(merged, "sample");
        if (sample.isEmpty())
        {
            instrument.warnings.add("SFZ region has no sample and was skipped");
            continue;
        }
        const auto defaultPath = stringOpcode(merged, "default_path");
        const auto key = defaultPath + "\n" + sample;
        juce::File resolved;
        juce::String resolveError;
        const auto cached = resolvedByKey.find(key);
        if (cached != resolvedByKey.end()) resolved = cached->second;
        else
        {
            const auto found = resolveErrors.find(key);
            resolveError = found != resolveErrors.end() ? found->second
                                                        : juce::String();
            if (found == resolveErrors.end())
            {
                resolved = resolveSample(file, defaultPath, sample, resolveError);
                if (resolved == juce::File()) resolveErrors[key] = resolveError;
                else resolvedByKey[key] = resolved;
            }
        }
        if (resolved == juce::File())
        {
            instrument.partial = true;
            instrument.warnings.add(resolveError);
            if (missingSource.count(key) == 0 && (int) instrument.sources.size() < maxSources)
            {
                SampleDescriptor descriptor;
                descriptor.id = (int) instrument.sources.size();
                descriptor.name = juce::File(normaliseSeparators(sample)).getFileName();
                descriptor.error = resolveError;
                missingSource[key] = descriptor.id;
                instrument.sources.push_back(std::move(descriptor));
                instrument.audio.push_back({});
            }
            continue;
        }
        const auto pathKey = resolved.getFullPathName();
        int sourceIndex = -1;
        const auto existing = sourceByPath.find(pathKey);
        if (existing != sourceByPath.end()) sourceIndex = existing->second;
        else
        {
            if ((int) instrument.sources.size() >= maxSources)
            {
                error = "Instrument has too many samples";
                return false;
            }
            SampleDescriptor descriptor;
            descriptor.id = (int) instrument.sources.size();
            descriptor.name = resolved.getFileName();
            juce::String decodeError;
            auto audio = decodeAudio(resolved, formats, decodedBytes, decodeError);
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
            sourceIndex = descriptor.id;
            sourceByPath[pathKey] = sourceIndex;
            instrument.sources.push_back(std::move(descriptor));
            instrument.audio.push_back(std::move(audio));
        }
        if (instrument.audio[(size_t) sourceIndex] == nullptr) continue;
        const auto& audio = *instrument.audio[(size_t) sourceIndex];

        ExternalZone zone;
        zone.sampleIndex = sourceIndex;
        zone.keyLow = juce::jlimit(0, 127, noteNumber(stringOpcode(merged, "lokey"), 0));
        zone.keyHigh = juce::jlimit(0, 127, noteNumber(stringOpcode(merged, "hikey"), 127));
        if (merged.count("key") != 0)
        {
            const int centre = noteNumber(merged["key"], 60);
            if (merged.count("lokey") == 0) zone.keyLow = centre;
            if (merged.count("hikey") == 0) zone.keyHigh = centre;
        }
        if (zone.keyLow > zone.keyHigh) std::swap(zone.keyLow, zone.keyHigh);
        zone.velocityLow = juce::jlimit(0, 127, intOpcode(merged, "lovel", 0));
        zone.velocityHigh = juce::jlimit(0, 127, intOpcode(merged, "hivel", 127));
        if (zone.velocityLow > zone.velocityHigh) std::swap(zone.velocityLow, zone.velocityHigh);
        zone.rootKey = juce::jlimit(0, 127,
            noteNumber(stringOpcode(merged, "pitch_keycenter"),
                       merged.count("key") != 0 ? noteNumber(merged["key"], 60) : 60));
        zone.tuningCents = doubleOpcode(merged, "tune", 0.0)
                         + doubleOpcode(merged, "transpose", 0.0) * 100.0
                         + doubleOpcode(merged, "finetune", 0.0);
        zone.attenuationDb = -doubleOpcode(merged, "volume", 0.0);
        zone.pan = juce::jlimit(-1.0, 1.0, doubleOpcode(merged, "pan", 0.0) / 100.0);
        zone.start = juce::jlimit<int64_t>(0, audio.frames,
                                           (int64_t) doubleOpcode(merged, "offset", 0.0));
        const int64_t declaredEnd = (int64_t) doubleOpcode(merged, "end", 0.0);
        if (declaredEnd > 0) zone.end = juce::jlimit(zone.start, audio.frames, declaredEnd + 1);
        else zone.end = audio.frames;
        if (zone.end <= zone.start)
        {
            instrument.warnings.add("SFZ region starts at or past the end of its sample: " + sample);
            continue;
        }
        const auto loopMode = stringOpcode(merged, "loopmode").isNotEmpty()
            ? stringOpcode(merged, "loopmode") : stringOpcode(merged, "loop_mode");
        const bool hasLoopPoints = merged.count("loop_start") != 0 || merged.count("loop_end") != 0
                                || merged.count("loopstart") != 0 || merged.count("loopend") != 0;
        const bool loopRequested = loopMode == "loop_continuous" || loopMode == "loop_sustain"
                                || loopMode == "continuous" || loopMode == "sustain";
        const bool oneShot = loopMode == "one_shot" || loopMode == "oneshot";
        if (! oneShot && (loopRequested || (loopMode.isEmpty() && hasLoopPoints)))
        {
            zone.loopStart = juce::jlimit<int64_t>(zone.start, zone.end,
                (int64_t) doubleOpcode(merged, "loop_start",
                    doubleOpcode(merged, "loopstart", (double) zone.start)));
            const int64_t loopEndInclusive = (int64_t) doubleOpcode(merged, "loop_end",
                doubleOpcode(merged, "loopend", (double) (zone.end - 1)));
            zone.loopEnd = juce::jlimit(zone.start, zone.end, loopEndInclusive + 1);
            zone.looping = zone.loopEnd > zone.loopStart;
        }
        zone.oneShot = oneShot;
        instrument.zones.push_back(zone);
        auto& descriptor = instrument.sources[(size_t) sourceIndex];
        descriptor.zones.push_back({ zone.keyLow, zone.keyHigh, zone.velocityLow, zone.velocityHigh });
        if (! descriptor.looping && zone.looping)
        {
            descriptor.looping = true;
            descriptor.loopStart = zone.loopStart;
            descriptor.loopEnd = zone.loopEnd;
        }
    }
    if (instrument.zones.empty())
    {
        error = instrument.warnings.isEmpty() ? "SFZ instrument has no playable regions"
                                              : instrument.warnings.joinIntoString("; ");
        return false;
    }
    return true;
}
} // namespace sonicator
