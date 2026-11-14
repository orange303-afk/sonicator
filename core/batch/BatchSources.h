#pragma once
#include "BatchTypes.h"
#include "core/database/SampleRepository.h"
#include "core/scanner/FileScanner.h"

namespace sonicator::batch
{
// Reads only the file header and identity; never decodes audio and never touches the message
// thread. `root` is the registered root used for relative destination layout.
struct SourceProbe
{
    bool ok = false;
    ErrorCode code = ErrorCode::none;
    juce::String message;
    SourceSnapshot snapshot;
    bool hasSnapshot = false;
};

SourceProbe probeSource(const juce::File&, int64_t sampleId, const juce::String& root,
                        CancelCheck);

// Longest registered root that contains the path (separator aware). Empty when none.
juce::String registeredRootFor(const juce::String& path, const juce::StringArray& roots);

// Snapshot of the request: rows expand stereo pairs into both physical mono files; a folder is
// enumerated recursively from the catalog without table filters or pagination. The result is
// immutable and never re-reads the catalog.
PreflightResult prepareBatch(SampleRepository&, const Request&, CancelCheck);
} // namespace sonicator::batch
