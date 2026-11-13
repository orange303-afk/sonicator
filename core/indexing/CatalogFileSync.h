#pragma once
#include "core/database/SampleRepository.h"
#include "core/scanner/FileScanner.h"

namespace sonicator
{
struct CatalogSyncResult
{
    bool ok = false;
    int64_t sampleId = 0;
    juce::String error;
};

// Reanalyzes one existing catalog row after its bytes changed. Tags, star, smart data and the row
// id survive because upsert updates the row in place; ML feedback keeps its labels and gets its
// audio-derived values (hash, v1, v2) recomputed so the contribution pass re-sends it.
CatalogSyncResult refreshCatalogFile(SampleRepository&, const juce::File&, const CancelCheck& = {});

// Adds one written file as a catalog row, then copies the template's user tags and star. A
// template id that is not in the catalog (0, or a source that is not a row) just yields a plain
// row.
CatalogSyncResult addCatalogCopy(SampleRepository&, const juce::File&, int64_t templateSampleId,
                                 const CancelCheck& = {});
} // namespace sonicator
