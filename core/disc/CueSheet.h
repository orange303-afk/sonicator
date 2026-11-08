#pragma once
#include "DiscTypes.h"
#include "ReadOnlyByteSource.h"
#include "SectorLayout.h"
namespace sonicator {
enum class CueTrackMode { audio, mode1_2048, mode1_2352, mode2_2352 };
struct CueTrack
{
    int number = 0;
    CueTrackMode mode = CueTrackMode::audio;
    int fileIndex = -1;
    uint64_t index00Frames = 0, index01Frames = 0, pregapFrames = 0, postgapFrames = 0;
    uint64_t endFrames = 0; // exclusive, within the track's FILE
    bool hasIndex00 = false, hasIndex01 = false;
    bool isData() const { return mode != CueTrackMode::audio; }
    SectorProfile profile() const
    {
        switch (mode)
        {
            case CueTrackMode::mode1_2048: return SectorProfile::cooked2048;
            case CueTrackMode::mode1_2352: return SectorProfile::mode1_2352;
            case CueTrackMode::mode2_2352: return SectorProfile::xaForm1_2352;
            case CueTrackMode::audio: break;
        }
        return SectorProfile::cooked2048;
    }
};
struct CueFileRef
{
    juce::String declaredName;
    juce::File resolved;
    uint64_t bytes = 0;
};
struct CueSheet
{
    std::vector<CueFileRef> files;
    std::vector<CueTrack> tracks;
    std::vector<int> dataTrackIndices() const
    {
        std::vector<int> indices;
        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].isData()) indices.push_back((int) i);
        return indices;
    }
};
struct CueTrackLayout
{
    juce::File file;
    SectorProfile profile = SectorProfile::cooked2048;
    uint64_t fileOffset = 0;
    uint64_t sectorCount = 0;
    uint64_t startFrames = 0;
};
// Pure text parse. FILE names are resolved and checked separately so a parser error cannot touch
// the file system.
DiscResult<CueSheet> parseCueSheet(std::string_view text);
// Resolves relative BINARY FILE references inside the cue's directory, fills sizes and end frames.
DiscResult<CueSheet> resolveCueSheet(CueSheet sheet, const juce::File& cueFile, const DiscLimits& limits);
DiscResult<CueSheet> openCueSheet(const juce::File& cueFile, const DiscLimits& limits, CancelCheck cancel);
// Byte offset/length of one data track. Audio tracks and mixed-stride files are refused.
DiscResult<CueTrackLayout> cueTrackLayout(const CueSheet& sheet, int trackIndex);
} // namespace sonicator
