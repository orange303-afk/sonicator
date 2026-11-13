#pragma once
#include "AudioEditBuffer.h"

namespace sonicator::edit
{
// Returns the markers as they apply after `operation` changed `range` on a buffer that had
// `framesBefore` frames. Operations that do not move frames return the input untouched.
FileMarkers transformMarkers(const FileMarkers&, EditOperation, FrameRange range,
                             int64_t framesBefore);
} // namespace sonicator::edit
