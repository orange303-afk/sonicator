#include "AudioEditMarkers.h"
#include <algorithm>

namespace sonicator::edit
{
namespace
{
int64_t framesAfter(const EditOperation op, const FrameRange range, const int64_t framesBefore)
{
    switch (op)
    {
        case EditOperation::deleteRegion: return framesBefore - range.length();
        case EditOperation::cropToSelection: return range.length();
        case EditOperation::trimSilence: return range.length();
        default: return framesBefore;
    }
}

bool mapPoint(const EditOperation op, const FrameRange range, const int64_t p, int64_t& mapped)
{
    switch (op)
    {
        case EditOperation::deleteRegion:
            if (p >= range.start && p < range.end) return false;
            mapped = p >= range.end ? p - range.length() : p;
            return true;
        case EditOperation::cropToSelection:
            if (p < range.start || p >= range.end) return false;
            mapped = p - range.start;
            return true;
        case EditOperation::reverse:
            if (p < range.start || p >= range.end)
            {
                mapped = p;
                return true;
            }
            mapped = range.start + range.end - 1 - p;
            return true;
        case EditOperation::trimSilence:
            if (p < range.start || p >= range.end) return false;
            mapped = p - range.start;
            return true;
        default:
            mapped = p;
            return true;
    }
}

bool mapLoop(const EditOperation op, const FrameRange range, int64_t start, int64_t end,
             int64_t& mappedStart, int64_t& mappedEnd)
{
    switch (op)
    {
        case EditOperation::deleteRegion:
            if (start >= range.start && end <= range.end) return false;
            mappedStart = start >= range.end ? start - range.length()
                : (start >= range.start ? range.start : start);
            mappedEnd = end >= range.end ? end - range.length()
                : (end >= range.start ? range.start : end);
            return true;
        case EditOperation::cropToSelection:
            mappedStart = std::max(start, range.start) - range.start;
            mappedEnd = std::min(end, range.end) - range.start;
            return true;
        case EditOperation::reverse:
            // Only a loop fully inside the reversed range has an exact mirror; a partial overlap
            // would need a split loop, which WAV cannot express.
            if (start < range.start || end > range.end) return false;
            mappedStart = range.start + range.end - end;
            mappedEnd = range.start + range.end - start;
            return true;
        case EditOperation::trimSilence:
            mappedStart = std::max(start, range.start) - range.start;
            mappedEnd = std::min(end, range.end) - range.start;
            return true;
        default:
            mappedStart = start;
            mappedEnd = end;
            return true;
    }
}
} // namespace

FileMarkers transformMarkers(const FileMarkers& markers, const EditOperation operation,
                             const FrameRange range, const int64_t framesBefore)
{
    switch (operation)
    {
        case EditOperation::deleteRegion:
        case EditOperation::cropToSelection:
        case EditOperation::reverse:
        case EditOperation::trimSilence:
            if (! range.valid() || range.end > framesBefore) return markers;
            break;
        default:
            return markers;
    }
    const auto total = framesAfter(operation, range, framesBefore);
    FileMarkers out;
    out.rootKey = markers.rootKey;
    if (markers.hasLoop)
    {
        int64_t start = 0, end = 0;
        if (mapLoop(operation, range, markers.loopStart, markers.loopEndExclusive, start, end)
            && start >= 0 && end > start && end <= total)
        {
            out.hasLoop = true;
            out.loopStart = start;
            out.loopEndExclusive = end;
        }
    }
    for (auto cue : markers.cuePositions)
    {
        int64_t mapped = 0;
        if (mapPoint(operation, range, cue, mapped) && mapped >= 0 && mapped < total)
            out.cuePositions.push_back(mapped);
    }
    return out;
}
} // namespace sonicator::edit
