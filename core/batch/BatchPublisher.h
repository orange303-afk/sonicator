#pragma once
#include "BatchTypes.h"
#include "core/scanner/FileScanner.h"

namespace sonicator::batch
{
// Every physical identity and exact path that no output may replace, including skipped inputs.
struct InputProtectionSet
{
    std::vector<juce::String> identities;
    std::vector<juce::String> paths; // lower-case full paths
    static InputProtectionSet from(const PreparedBatch&);
    bool protects(const juce::File&) const;
};

struct PublishResult
{
    bool ok = false;
    juce::File finalPath;
    ErrorCode code = ErrorCode::none;
    juce::String error;
};

// Unique temporary beside the target, so the final rename/replace stays on one volume.
juce::File createBatchTemporary(const juce::File& target);

// Commits a fully written and validated temporary. Rename uses an atomic no-clobber link and moves
// to the next free name on an external race. Replace verifies that the old output did not change
// and refuses to overwrite an input or its hard-link/symlink alias. There is no delete-then-copy
// fallback; a filesystem without the required primitive gets publish_failed.
PublishResult publishBatchFile(const juce::File& temporary, const OutputPlan&, ConflictPolicy,
                               const InputProtectionSet&, const SourceSnapshot&, CancelCheck);
} // namespace sonicator::batch
