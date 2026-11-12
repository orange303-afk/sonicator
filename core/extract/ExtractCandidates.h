#pragma once
#include "DiscExtract.h"
#include "core/database/SampleRecord.h"
#include <functional>
#include <vector>
namespace sonicator {
// What "Extract samples…" would run for the current selection: embedded banks, whole disc images,
// or just the selected sample records of an image. Rows that are not extractable are ignored.
struct ExtractCandidates
{
    std::vector<juce::File> embedded;
    std::vector<juce::File> images;
    std::vector<DiscExtractJob> entries;
    int sourceCount() const { return (int) (embedded.size() + images.size() + entries.size()); }
    bool empty() const { return sourceCount() == 0; }
};
// `ignored` hides ignored catalog paths (the library already knows them).
ExtractCandidates collectExtractCandidates(const std::vector<SampleRecord>& selection,
                                           const std::function<bool(const juce::String&)>& ignored = {});
} // namespace sonicator
