#include "KrzFile.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <set>
#include <cstdio>
namespace sonicator::krz {
namespace {
constexpr int64_t maxFileSize = 256LL * 1024 * 1024;
constexpr int64_t objectSectionStart = 0x20;
constexpr int64_t continuationPcmStart = 0x24;

using Bytes = std::vector<std::byte>;

bool readWholeFile(const juce::File& file, Bytes& out, juce::String& error)
{
    if (! file.existsAsFile()) { error = "File is missing"; return false; }
    const auto size = file.getSize();
    if (size <= 0) { error = "File is empty"; return false; }
    if (size > maxFileSize) { error = "File is larger than 256 MB"; return false; }
    out.resize((size_t) size);
    juce::FileInputStream input(file);
    if (! input.openedOk()) { error = "Cannot open " + file.getFileName(); return false; }
    if (input.read(out.data(), (int) size) != (int) size)
    {
        error = "Cannot read " + file.getFileName();
        return false;
    }
    return true;
}

bool inRange(int64_t at, int64_t length, int64_t size) { return at >= 0 && length >= 0 && at <= size && length <= size - at; }

bool rd16(const Bytes& data, int64_t at, int& value)
{
    if (! inRange(at, 2, (int64_t) data.size())) return false;
    value = ((int) std::to_integer<int>(data[(size_t) at]) << 8) | std::to_integer<int>(data[(size_t) at + 1]);
    return true;
}
bool rd32(const Bytes& data, int64_t at, juce::uint32& value)
{
    if (! inRange(at, 4, (int64_t) data.size())) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | (juce::uint32) std::to_integer<int>(data[(size_t) at + i]);
    return true;
}
bool rdSigned32(const Bytes& data, int64_t at, int32_t& value)
{
    juce::uint32 raw = 0;
    if (! rd32(data, at, raw)) return false;
    value = (int32_t) raw;
    return true;
}

juce::String readName(const Bytes& data, int64_t from, int64_t to)
{
    juce::String name;
    for (auto at = from; at < to; ++at)
    {
        const auto c = std::to_integer<int>(data[(size_t) at]);
        if (c == 0) break;
        name += (juce::juce_wchar) (unsigned char) c;
    }
    return name.trim();
}

struct ObjectView
{
    int type = 0, id = 0;
    juce::String name;
    int64_t body = 0, bodyEnd = 0;
};

// Walks one block from `start` to `end`, calling `visit` for every object. A malformed object ends
// the walk with an error instead of reading outside the block.
bool walkBlock(const Bytes& data, int64_t start, int64_t end,
               const std::function<bool(const ObjectView&)>& visit, juce::String& error)
{
    int64_t at = start;
    int visited = 0;
    while (at + 10 <= end)
    {
        int hash = 0, bodySize = 0, nameOfs = 0;
        if (! rd16(data, at + 4, hash) || ! rd16(data, at + 6, bodySize) || ! rd16(data, at + 8, nameOfs))
        {
            error = "Object header is truncated";
            return false;
        }
        // The object starts at the hash (block start + 4); the body size is measured from there.
        const int64_t body = at + 8 + nameOfs;
        // The stored size runs from the hash to the end of the body; the block bounds it.
        const int64_t bodyEnd = std::min(end, at + 4 + (int64_t) bodySize);
        const int type = hash >> 10;
        const bool parsed = type == 36 || type == 37 || type == 38;
        // Unknown object kinds (effects, tables) do not always follow the name-offset convention;
        // they are skipped by their size, so only their extent has to be inside the block.
        if (bodyEnd < at + 6 || at + 4 + (int64_t) bodySize > end + 8
            || (parsed && (body < at + 10 || body > bodyEnd)))
        {
            // Trailing bytes after the objects this reader understands are common in effect banks.
            if (visited > 0) break;
            error = "Object extends past its block";
            return false;
        }
        ObjectView view;
        view.type = type;
        view.id = hash & 0x3ff;
        view.name = readName(data, at + 10, body);
        view.body = body;
        view.bodyEnd = bodyEnd;
        if (! visit(view)) return false;
        ++visited;
        int64_t next = at + 6 + (int64_t) bodySize;
        if (next % 2 != 0) ++next;
        if (next <= at) { error = "Object size does not advance"; return false; }
        at = next;
    }
    return true;
}

int tuneWidth(int method)
{
    if ((method & 0x10) != 0) return 2;
    if (method == 0x09 || method == 0x0b || method == 0x0d || method == 0x0f) return 1;
    return 0;
}
bool hasVolume(int method)
{
    return method == 0x05 || method == 0x07 || method == 0x0d || method == 0x0f
        || method == 0x15 || method == 0x17;
}
bool hasSampleId(int method)
{
    return method == 0x03 || method == 0x07 || method == 0x0b || method == 0x0f
        || method == 0x13 || method == 0x17;
}

bool parseSample(const Bytes& data, const ObjectView& view, Bank& bank, juce::String& error)
{
    int headerCount = 1, flags = 0;
    if (! rd16(data, view.body + 2, headerCount))
    {
        error = "Sample object is truncated";
        return false;
    }
    headerCount = std::clamp(headerCount + 1, 1, 8);
    if (! inRange(view.body + 6, 1, (int64_t) data.size())) { error = "Sample object is truncated"; return false; }
    flags = std::to_integer<int>(data[(size_t) view.body + 6]);
    if (! inRange(view.body + 12, headerCount * 32, view.bodyEnd))
    {
        error = "Sample headers extend past the object";
        return false;
    }
    SampleObject sample;
    sample.id = view.id;
    sample.name = view.name;
    sample.stereo = (flags & 1) != 0;
    for (int i = 0; i < headerCount; ++i)
    {
        const auto at = view.body + 12 + 32 * (int64_t) i;
        SoundFileHeader header;
        header.rootKey = std::to_integer<int>(data[(size_t) at]);
        const int headerFlags = std::to_integer<int>(data[(size_t) at + 1]);
        header.looping = (headerFlags & 0x80) == 0;
        header.ramBased = (headerFlags & 0x10) != 0;
        header.playbackMode = headerFlags & 0x03;
        header.volumeHalfDb = (int8_t) std::to_integer<int>(data[(size_t) at + 2]);
        juce::uint32 startWord = 0, loopWord = 0, endWord = 0, period = 0;
        if (! rd32(data, at + 8, startWord) || ! rd32(data, at + 16, loopWord)
            || ! rd32(data, at + 20, endWord) || ! rd32(data, at + 28, period))
        {
            error = "Sample header is truncated";
            return false;
        }
        header.startWord = (int64_t) startWord;
        header.loopWord = (int64_t) loopWord;
        header.endWord = (int64_t) endWord;
        header.sampleRate = period > 0 ? (int) std::lround(1.0e9 / (double) period) : 0;
        sample.headers.push_back(header);
    }
    bank.samples.push_back(std::move(sample));
    return true;
}

bool parseKeymap(const Bytes& data, const ObjectView& view, Bank& bank, juce::String& error)
{
    int defaultId = 0, method = 0, entriesMinusOne = 0, entrySize = 0;
    if (! rd16(data, view.body, defaultId) || ! rd16(data, view.body + 2, method)
        || ! rd16(data, view.body + 8, entriesMinusOne) || ! rd16(data, view.body + 10, entrySize))
    {
        error = "Keymap header is truncated";
        return false;
    }
    if (entrySize < 1 || entrySize > 8)
    {
        error = "Keymap entry size " + juce::String(entrySize) + " is not supported";
        return false;
    }
    const int entries = std::clamp(entriesMinusOne + 1, 1, 128);
    Keymap keymap;
    keymap.id = view.id;
    keymap.name = view.name;
    keymap.defaultSampleId = defaultId;
    for (auto& level : keymap.levels) level.assign(128, KeymapEntry {});
    const int tune = tuneWidth(method);
    for (int level = 0; level < 8; ++level)
    {
        int offset = 0;
        if (! rd16(data, view.body + 12 + 2 * level, offset))
        {
            error = "Keymap velocity table is truncated";
            return false;
        }
        const int64_t base = view.body + 12 + 2 * level + offset;
        if (! inRange(base, (int64_t) entries * entrySize, view.bodyEnd))
        {
            error = "Keymap entries extend past the object";
            return false;
        }
        for (int slot = 0; slot < entries; ++slot)
        {
            const int note = slot + 12;
            if (note > 127) break;
            const int64_t at = base + (int64_t) slot * entrySize;
            int cursor = 0;
            KeymapEntry entry;
            if (tune == 1) entry.tuningCents = (int8_t) std::to_integer<int>(data[(size_t) at + cursor++]);
            else if (tune == 2)
            {
                entry.tuningCents = (int16_t) (((int) std::to_integer<int>(data[(size_t) at]) << 8)
                                               | std::to_integer<int>(data[(size_t) at + 1]));
                cursor += 2;
            }
            if (hasVolume(method)) entry.volumeHalfDb = (int8_t) std::to_integer<int>(data[(size_t) at + cursor++]);
            if (hasSampleId(method))
            {
                entry.sampleId = ((int) std::to_integer<int>(data[(size_t) at + cursor]) << 8)
                                 | std::to_integer<int>(data[(size_t) at + cursor + 1]);
                cursor += 2;
            }
            if (cursor < entrySize) entry.subSample = std::to_integer<int>(data[(size_t) at + cursor]);
            else entry.subSample = 1;
            if (entry.sampleId == 0) entry.sampleId = defaultId;
            keymap.levels[(size_t) level][(size_t) note] = entry;
        }
    }
    bank.keymaps.push_back(std::move(keymap));
    return true;
}

int segmentLength(int tag)
{
    if (tag == 0x0f) return 7;
    switch (tag & 0xf8)
    {
        case 0x08: case 0x20: case 0x50: return 15;
        case 0x10: case 0x14: case 0x18: case 0x68: return tag == 0x18 ? 3 : 7;
        case 0x40: case 0x78: return 31;
        case 0x00: return 0;
        default: return -1;
    }
}

bool parseProgram(const Bytes& data, const ObjectView& view, Bank& bank, juce::String& error)
{
    Program program;
    program.id = view.id;
    program.name = view.name;
    std::vector<int> keymap1, keymap2;
    int64_t at = view.body;
    while (at + 2 <= view.bodyEnd)
    {
        const int tag = std::to_integer<int>(data[(size_t) at]);
        if (tag == 0 && std::to_integer<int>(data[(size_t) at + 1]) == 0) break;
        // An unknown or truncated segment ends this program: the layers read so far stay usable,
        // the rest of the file is untouched. VAST parameters are not interpreted anyway.
        const int length = segmentLength(tag);
        if (length <= 0 || ! inRange(at + 1, length, view.bodyEnd))
        {
            bank.warnings.add("Program " + program.name + ": stops at segment 0x"
                              + juce::String::toHexString(tag));
            break;
        }
        const int64_t seg = at + 1;
        if (tag == 0x09 && length == 15)
        {
            keymap1.push_back(0);
            keymap2.push_back(0);
        }
        else if (tag == 0x40 && length == 31 && ! keymap1.empty())
        {
            int first = 0, second = 0;
            rd16(data, seg + 11, first);
            rd16(data, seg + 7, second);
            keymap1.back() = first;
            keymap2.back() = second;
        }
        at += 1 + length;
    }
    for (size_t i = 0; i < keymap1.size(); ++i)
    {
        Layer layer;
        layer.keymap1 = keymap1[i];
        layer.keymap2 = keymap2[i];
        program.layers.push_back(layer);
    }
    bank.programs.push_back(std::move(program));
    return true;
}

// "sound.kr1.krz" -> "sound"; "sound.krz" -> "sound".
juce::String baseStem(const juce::File& file)
{
    auto stem = file.getFileNameWithoutExtension();
    const int dot = stem.lastIndexOfChar('.');
    if (dot > 0)
    {
        const auto tail = stem.substring(dot);
        if (tail.startsWithIgnoreCase(".kr") && tail.substring(3).containsOnly("0123456789"))
            stem = stem.substring(0, dot);
    }
    return stem;
}
juce::String partName(const juce::File& firstPart, int number)
{
    return baseStem(firstPart) + ".kr" + juce::String(number) + firstPart.getFileExtension();
}

juce::File partFile(const juce::File& firstPart, int number)
{
    return firstPart.getParentDirectory().getChildFile(partName(firstPart, number));
}

bool readPcm(const Bytes& data, int64_t start, std::vector<juce::int16>& out, juce::String& error)
{
    if (! inRange(start, 0, (int64_t) data.size()) || start < 0)
    {
        error = "Sample data offset is past the end of the file";
        return false;
    }
    const int64_t words = ((int64_t) data.size() - start) / 2;
    out.reserve(out.size() + (size_t) words);
    for (int64_t i = 0; i < words; ++i)
    {
        const auto at = start + 2 * i;
        const int value = ((int) std::to_integer<int>(data[(size_t) at]) << 8)
                          | std::to_integer<int>(data[(size_t) at + 1]);
        out.push_back((juce::int16) (int16_t) value);
    }
    return true;
}
} // namespace

bool isKrzFile(const juce::File& file)
{
    if (! file.hasFileExtension("krz;k25;k26")) return false;
    juce::FileInputStream input(file);
    if (! input.openedOk()) return false;
    char magic[4] {};
    if (input.read(magic, 4) != 4) return false;
    return juce::String(magic, 4) == "PRAM" || juce::String(magic, 4) == "SROM";
}

int continuationPartNumber(const juce::File& file)
{
    if (! file.hasFileExtension("krz;k25;k26")) return 0;
    const auto stem = file.getFileNameWithoutExtension();
    if (! stem.endsWithIgnoreCase(".kr") && ! stem.containsChar('.')) return 0;
    const int dot = stem.lastIndexOfChar('.');
    if (dot <= 0) return 0;
    const auto tail = stem.substring(dot);            // ".kr2"
    if (! tail.startsWithIgnoreCase(".kr")) return 0;
    const auto digits = tail.substring(3);
    if (digits.isEmpty() || ! digits.containsOnly("0123456789")) return 0;
    const int number = digits.getIntValue();
    return number >= 1 && number < 1000 ? number : 0;
}

juce::File firstPartOf(const juce::File& file)
{
    const int part = continuationPartNumber(file);
    if (part <= 1) return file;
    return partFile(file, 1);
}

bool readBank(const juce::File& file, Bank& bank, juce::String& error)
{
    error.clear();
    bank = Bank {};
    if (! file.existsAsFile()) { error = "File is missing"; return false; }
    const auto first = firstPartOf(file);
    Bytes data;
    if (! readWholeFile(first, data, error)) return false;
    bank.magic = juce::String((const char*) data.data(), 4);
    if (bank.magic != "PRAM" && bank.magic != "SROM")
    {
        error = "Not a Kurzweil K2000/K2500/K2600 bank";
        return false;
    }
    if (bank.magic == "SROM")
    {
        error = "SROM effects and sample-ROM files are not object banks";
        return false;
    }
    juce::uint32 pcmStart = 0;
    if (! rd32(data, 4, pcmStart))
    {
        error = "File header is truncated";
        return false;
    }
    if (pcmStart < (juce::uint32) objectSectionStart || (int64_t) pcmStart > (int64_t) data.size())
    {
        error = "Sample data offset is past the end of the file";
        return false;
    }

    // Objects live in blocks; each block opens with a negative length.
    int64_t at = objectSectionStart;
    while (at + 4 <= (int64_t) pcmStart)
    {
        int32_t blockLength = 0;
        if (! rdSigned32(data, at, blockLength))
        {
            error = "Object block is truncated";
            return false;
        }
        if (blockLength == 0) break;
        if (blockLength > 0 || at - (int64_t) blockLength > (int64_t) pcmStart)
        {
            error = "Object block length is out of range";
            return false;
        }
        const int64_t blockEnd = at - (int64_t) blockLength;
        bool failed = false;
        const bool walked = walkBlock(data, at, blockEnd, [&] (const ObjectView& view)
        {
            bool ok = true;
            if (view.type == 38) ok = parseSample(data, view, bank, error);
            else if (view.type == 37) ok = parseKeymap(data, view, bank, error);
            else if (view.type == 36) ok = parseProgram(data, view, bank, error);
            if (! ok) failed = true;
            return ok;
        }, error);
        if (! walked) { if (error.isEmpty()) error = "Object block could not be walked"; return false; }
        if (failed) { if (error.isEmpty()) error = "object failed"; return false; }
        at = blockEnd;
    }

    // Every present part contributes its sample data; a continuation part starts at 0x24.
    if (! readPcm(data, (int64_t) pcmStart, bank.pcm, error)) return false;
    int lastPart = 1;
    for (int number = 2; number < 100; ++number)
    {
        const auto next = partFile(first, number);
        if (! next.existsAsFile()) break;
        Bytes tail;
        juce::String tailError;
        if (! readWholeFile(next, tail, tailError))
        {
            bank.warnings.add(next.getFileName() + ": " + tailError);
            break;
        }
        if (! readPcm(tail, continuationPcmStart, bank.pcm, error))
        {
            bank.warnings.add(next.getFileName() + ": " + error);
            break;
        }
        lastPart = number;
    }
    // A span that reaches past the collected data means a part is missing.
    int64_t highestEnd = -1;
    for (const auto& sample : bank.samples)
        for (const auto& header : sample.headers)
            if (header.ramBased) highestEnd = std::max(highestEnd, header.endWord);
    if (highestEnd >= (int64_t) bank.pcm.size())
    {
        const auto missing = partName(first, lastPart + 1);
        bank.missingPart = missing;
        bank.warnings.add("Missing part: " + missing);
    }
    return true;
}

bool decodeHeader(const Bank& bank, const SampleObject& sample, int headerIndex, DecodedSample& out,
                  juce::String& error)
{
    out = DecodedSample {};
    error.clear();
    if (headerIndex < 0 || headerIndex >= (int) sample.headers.size())
    {
        error = "Sample header index is out of range";
        return false;
    }
    const auto& header = sample.headers[(size_t) headerIndex];
    if (! header.ramBased)
    {
        error = "Sample " + sample.name + " lives in the K2000 factory ROM";
        return false;
    }
    if (header.sampleRate <= 0 || header.sampleRate < 1000 || header.sampleRate > 384000)
    {
        error = "Sample " + sample.name + " has no usable rate";
        return false;
    }
    // A one-shot header stores its own last word; a looped header stores the loop end there, so its
    // audio ends where the next span begins (or where the sample data ends).
    int64_t audioEnd = header.looping ? (int64_t) bank.pcm.size() - 1 : header.endWord;
    if (header.looping)
        for (const auto& other : sample.headers)
            if (other.startWord > header.startWord && other.startWord - 1 < audioEnd)
                audioEnd = std::min(audioEnd, other.startWord - 1);
    if (header.startWord < 0 || header.endWord < header.startWord
        || header.startWord >= (int64_t) bank.pcm.size()
        || header.endWord >= (int64_t) bank.pcm.size()
        || audioEnd < header.startWord || audioEnd >= (int64_t) bank.pcm.size())
    {
        error = bank.missingPart.isNotEmpty()
            ? "Sample " + sample.name + " needs the missing part " + bank.missingPart
            : "Sample " + sample.name + " lies outside the sample data";
        return false;
    }
    const auto frames = audioEnd - header.startWord + 1;
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = header.sampleRate;
    audio->channels = 1;
    audio->frames = frames;
    audio->left.resize((size_t) frames);
    for (int64_t i = 0; i < frames; ++i)
        audio->left[(size_t) i] = (float) bank.pcm[(size_t) (header.startWord + i)] / 32768.0f;
    out.audio = audio;
    out.rootKey = header.rootKey;
    out.looping = header.looping;
    if (header.looping)
    {
        const auto loopStart = header.loopWord - header.startWord;
        const auto loopEnd = std::min(header.endWord, audioEnd) - header.startWord + 1;
        if (loopStart >= 0 && loopEnd > loopStart && loopEnd <= frames)
        {
            out.loopStart = loopStart;
            out.loopEnd = loopEnd;
        }
        else
        {
            out.looping = false;
        }
    }
    return true;
}

bool decodeStereo(const Bank& bank, const SampleObject& sample, DecodedSample& out, juce::String& error)
{
    out = DecodedSample {};
    error.clear();
    if (! sample.stereo || sample.headers.size() < 2)
    {
        error = "Sample " + sample.name + " is not stereo";
        return false;
    }
    DecodedSample left, right;
    if (! decodeHeader(bank, sample, 0, left, error)) return false;
    if (! decodeHeader(bank, sample, 1, right, error)) return false;
    if (left.audio->frames != right.audio->frames)
    {
        error = "Sample " + sample.name + " has mismatched stereo halves";
        return false;
    }
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = left.audio->sampleRate;
    audio->channels = 2;
    audio->frames = left.audio->frames;
    audio->left = left.audio->left;
    audio->right = right.audio->left;
    out.audio = audio;
    out.rootKey = left.rootKey;
    out.looping = left.looping;
    out.loopStart = left.loopStart;
    out.loopEnd = left.loopEnd;
    return true;
}
} // namespace sonicator::krz
