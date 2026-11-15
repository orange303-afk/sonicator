#pragma once
#include "BatchPipeline.h"

namespace sonicator::batch
{
struct SplitRange
{
    int64_t begin = 0;
    int64_t end = 0;
};

struct BatchSplitDetection
{
    std::vector<SplitRange> parts;
    int discardedShortParts = 0;
    bool split = false; // false when the stream stays whole
};

// Scans the already prepared pre-split stream. Fewer than two parts means the stream stays whole.
BatchSplitDetection detectBatchParts(PreparedAudio&, const SplitSilence&, CancelCheck = {});
bool recipeHasSplit(const Recipe&);
int recipeSplitIndex(const Recipe&);
const SplitSilence* recipeSplit(const Recipe&);
} // namespace sonicator::batch
