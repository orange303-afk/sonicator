#include "CueSheet.h"
#include <cstring>
#include <sstream>
namespace sonicator {
namespace {
constexpr uint64_t framesPerSecond = 75;
bool parseTime(const juce::String& text, uint64_t& frames)
{
    const auto parts = juce::StringArray::fromTokens(text, ":", "");
    if (parts.size() != 3) return false;
    for (const auto& part : parts)
        if (part.isEmpty() || ! part.containsOnly("0123456789")) return false;
    const auto minutes = parts[0].getLargeIntValue();
    const auto seconds = parts[1].getIntValue();
    const auto frame = parts[2].getIntValue();
    if (minutes < 0 || seconds < 0 || seconds >= 60 || frame < 0 || frame >= 75) return false;
    frames = (uint64_t) minutes * 60 * framesPerSecond + (uint64_t) seconds * framesPerSecond + (uint64_t) frame;
    return true;
}
bool isUnsafeReference(const juce::String& name)
{
    if (name.isEmpty()) return true;
    if (name.contains("://")) return true;
    if (name.startsWithChar('/') || name.startsWithChar('\\') || name.startsWithChar('~')) return true;
    if (name.length() > 1 && name[1] == ':') return true; // drive letter
    for (const auto& part : juce::StringArray::fromTokens(name, "/\\", ""))
        if (part == "..") return true;
    return false;
}
CueTrackMode parseMode(const juce::String& text, bool& recognised)
{
    recognised = true;
    if (text == "AUDIO") return CueTrackMode::audio;
    if (text == "MODE1/2048") return CueTrackMode::mode1_2048;
    if (text == "MODE1/2352") return CueTrackMode::mode1_2352;
    if (text == "MODE2/2352") return CueTrackMode::mode2_2352;
    recognised = false;
    return CueTrackMode::audio;
}
DiscResult<CueSheet> fail(DiscStatus status, const juce::String& message)
{
    DiscResult<CueSheet> result;
    result.status = status;
    result.error = message;
    return result;
}
} // namespace

DiscResult<CueSheet> parseCueSheet(std::string_view text)
{
    DiscResult<CueSheet> result;
    CueSheet sheet;
    int currentTrack = -1;
    int currentFile = -1;
    std::vector<std::pair<int, uint64_t>> indexZero;
    auto view = juce::String::fromUTF8(text.data(), (int) text.size());
    if (view.startsWithChar((juce::juce_wchar) 0xfeff)) view = view.substring(1);
    const auto lines = juce::StringArray::fromLines(view);
    for (const auto& raw : lines)
    {
        const auto line = raw.trim();
        if (line.isEmpty() || line.startsWithIgnoreCase("REM")) continue;
        const auto command = line.upToFirstOccurrenceOf(" ", false, false).toUpperCase();
        const auto rest = line.substring(command.length()).trim();
        if (command == "FILE")
        {
            juce::String name;
            juce::String type;
            if (rest.startsWithChar('"'))
            {
                const auto end = rest.indexOfChar(1, '"');
                if (end < 0) return fail(DiscStatus::malformed, "unterminated FILE name");
                name = rest.substring(1, end);
                type = rest.substring(end + 1).trim();
            }
            else
            {
                name = rest.upToFirstOccurrenceOf(" ", false, false);
                type = rest.substring(name.length()).trim();
            }
            if (! type.equalsIgnoreCase("BINARY"))
                return fail(DiscStatus::unsupported, "only FILE BINARY is supported");
            if (isUnsafeReference(name))
                return fail(DiscStatus::unsupported, "unsafe FILE reference: " + name);
            CueFileRef file;
            file.declaredName = name;
            sheet.files.push_back(file);
            currentFile = (int) sheet.files.size() - 1;
            currentTrack = -1;
            continue;
        }
        if (command == "TRACK")
        {
            const auto tokens = juce::StringArray::fromTokens(rest, " ", "");
            if (tokens.size() != 2 || currentFile < 0)
                return fail(DiscStatus::malformed, "TRACK requires number and mode after FILE");
            const auto number = tokens[0].getIntValue();
            if (number < 1 || number > 99 || ! tokens[0].containsOnly("0123456789"))
                return fail(DiscStatus::malformed, "track number must be 1..99");
            for (const auto& existing : sheet.tracks)
                if (existing.number == number) return fail(DiscStatus::malformed, "duplicate track number");
            bool recognised = false;
            const auto mode = parseMode(tokens[1].toUpperCase(), recognised);
            if (! recognised) return fail(DiscStatus::unsupported, "unsupported TRACK mode: " + tokens[1]);
            CueTrack track;
            track.number = number;
            track.mode = mode;
            track.fileIndex = currentFile;
            sheet.tracks.push_back(track);
            currentTrack = (int) sheet.tracks.size() - 1;
            continue;
        }
        if (currentTrack < 0) return fail(DiscStatus::malformed, "INDEX/PREGAP outside a TRACK");
        auto& track = sheet.tracks[(size_t) currentTrack];
        if (command == "INDEX")
        {
            const auto tokens = juce::StringArray::fromTokens(rest, " ", "");
            if (tokens.size() != 2) return fail(DiscStatus::malformed, "INDEX requires number and time");
            const auto number = tokens[0].getIntValue();
            uint64_t frames = 0;
            if (! parseTime(tokens[1], frames)) return fail(DiscStatus::malformed, "invalid INDEX time");
            if (number == 0)
            {
                track.index00Frames = frames;
                track.hasIndex00 = true;
            }
            else if (number == 1)
            {
                track.index01Frames = frames;
                track.hasIndex01 = true;
            }
            else return fail(DiscStatus::unsupported, "only INDEX 00 and 01 are supported");
            continue;
        }
        if (command == "PREGAP")
        {
            if (! parseTime(rest, track.pregapFrames)) return fail(DiscStatus::malformed, "invalid PREGAP time");
            continue;
        }
        if (command == "POSTGAP")
        {
            if (! parseTime(rest, track.postgapFrames)) return fail(DiscStatus::malformed, "invalid POSTGAP time");
            continue;
        }
        // TITLE/PERFORMER/CATALOG/ISRC/FLAGS and unknown metadata do not affect the layout.
    }
    for (const auto& track : sheet.tracks)
        if (track.isData() && ! track.hasIndex01)
            return fail(DiscStatus::malformed, "data track has no INDEX 01");
    result.status = DiscStatus::ready;
    result.value = std::move(sheet);
    return result;
}

DiscResult<CueSheet> resolveCueSheet(CueSheet sheet, const juce::File& cueFile, const DiscLimits& limits)
{
    if (sheet.files.empty() || sheet.tracks.empty())
        return fail(DiscStatus::malformed, "the cue sheet has no FILE/TRACK entries");
    const auto folder = cueFile.getParentDirectory();
    for (auto& file : sheet.files)
    {
        file.resolved = folder.getChildFile(file.declaredName);
        if (file.resolved.isSymbolicLink()) return fail(DiscStatus::unsupported, "FILE symlinks are not allowed");
        if (! file.resolved.isAChildOf(folder) && file.resolved != folder)
            return fail(DiscStatus::unsupported, "FILE escapes the cue directory");
        if (! file.resolved.existsAsFile()) return fail(DiscStatus::malformed, "FILE is missing: " + file.declaredName);
        file.bytes = (uint64_t) file.resolved.getSize();
    }
    // Each FILE must use one stride; a mixed 2048/2352 file cannot be addressed unambiguously.
    for (size_t fileIndex = 0; fileIndex < sheet.files.size(); ++fileIndex)
    {
        int stride = 0;
        for (const auto& track : sheet.tracks)
        {
            if (track.fileIndex != (int) fileIndex || ! track.isData()) continue;
            const int trackStride = track.mode == CueTrackMode::mode1_2048 ? 2048 : 2352;
            if (stride != 0 && stride != trackStride)
                return fail(DiscStatus::unsupported, "mixed 2048/2352 tracks in one FILE are not supported");
            stride = trackStride;
        }
    }
    for (size_t i = 0; i < sheet.tracks.size(); ++i)
    {
        auto& track = sheet.tracks[i];
        const auto& file = sheet.files[(size_t) track.fileIndex];
        const uint64_t stride = track.mode == CueTrackMode::mode1_2048 ? 2048 : 2352;
        uint64_t next = file.bytes / stride;
        for (size_t j = i + 1; j < sheet.tracks.size(); ++j)
        {
            const auto& candidate = sheet.tracks[j];
            if (candidate.fileIndex != track.fileIndex) break;
            if (candidate.index01Frames <= track.index01Frames)
                return fail(DiscStatus::malformed, "track start times are not increasing");
            next = candidate.index01Frames;
            break;
        }
        track.endFrames = next;
        if (track.index01Frames >= track.endFrames)
            return fail(DiscStatus::malformed, "track has no sectors");
        if (track.endFrames > file.bytes / stride)
            return fail(DiscStatus::malformed, "track extends past its FILE");
    }
    return { DiscStatus::ready, std::move(sheet), {} };
}

DiscResult<CueSheet> openCueSheet(const juce::File& cueFile, const DiscLimits& limits, CancelCheck cancel)
{
    if (cancel && cancel()) return fail(DiscStatus::cancelled, {});
    if (! cueFile.existsAsFile()) return fail(DiscStatus::ioError, "cue file does not exist");
    if ((uint64_t) cueFile.getSize() > limits.maxCueBytes) return fail(DiscStatus::limitExceeded, "cue file is too large");
    auto stream = cueFile.createInputStream();
    if (stream == nullptr) return fail(DiscStatus::ioError, "cue file cannot be opened");
    std::string text;
    text.resize((size_t) cueFile.getSize());
    if (! text.empty() && stream->read(text.data(), (int) text.size()) != (int) text.size())
        return fail(DiscStatus::ioError, "cue file cannot be read");
    auto parsed = parseCueSheet(text);
    if (! parsed.ok()) return parsed;
    return resolveCueSheet(std::move(parsed.value), cueFile, limits);
}

DiscResult<CueTrackLayout> cueTrackLayout(const CueSheet& sheet, int trackIndex)
{
    DiscResult<CueTrackLayout> result;
    if (trackIndex < 0 || trackIndex >= (int) sheet.tracks.size())
    {
        result.status = DiscStatus::unknown;
        result.error = "track index is out of range";
        return result;
    }
    const auto& track = sheet.tracks[(size_t) trackIndex];
    if (! track.isData())
    {
        result.status = DiscStatus::unsupported;
        result.error = "CDDA audio tracks are metadata-only in this stage";
        return result;
    }
    const auto& file = sheet.files[(size_t) track.fileIndex];
    const uint64_t stride = track.mode == CueTrackMode::mode1_2048 ? 2048 : 2352;
    if (track.index01Frames > file.bytes / stride)
    {
        result.status = DiscStatus::malformed;
        result.error = "track starts past the end of its FILE";
        return result;
    }
    result.status = DiscStatus::ready;
    result.value.file = file.resolved;
    result.value.profile = track.profile();
    result.value.startFrames = track.index01Frames;
    result.value.fileOffset = track.index01Frames * stride;
    result.value.sectorCount = track.endFrames - track.index01Frames;
    return result;
}
} // namespace sonicator
