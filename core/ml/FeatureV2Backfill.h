#pragma once
#include "core/database/SampleRepository.h"
#include "core/dsp/FeatureExtractorV2.h"
#include "core/scanner/FileScanner.h"

namespace sonicator {

struct FeatureV2Result
{
    int scanned = 0;
    int updated = 0;   // schema-2 vector stored
    int missing = 0;   // no row or no file on disk: stays on schema 1
    int failed = 0;    // file exists but cannot be decoded or described
};

// Fills the v2 vector for every confirmed row still on schema 1. One decode per file, bounded to the
// same 10 seconds the training CLI uses, so the vector matches the model. Missing files keep their
// schema-1 vector and are counted; nothing is deleted or rewritten to a guess.
// `maxRows` limits the pass for a manual check or a throttled run; 0 means every confirmed row.
FeatureV2Result backfillFeatureV2(SampleRepository& repository, const CancelCheck& cancel = {},
                                  int maxRows = 0);

} // namespace sonicator
