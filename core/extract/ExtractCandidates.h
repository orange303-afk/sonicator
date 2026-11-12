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
// How a batch is split for the service, which runs one mode at a time: embedded banks through the
// embedded reader, disc images (and selected records) through the disc reader.
struct ExtractRouting
{
    std::vector<juce::File> embedded;
    std::vector<DiscExtractJob> jobs;
};
// `files` may mix banks and images; an image already covered by an entry job is not added again.
ExtractRouting routeExtractSources(const std::vector<juce::File>& files,
                                   const std::vector<DiscExtractJob>& jobs);
// `ignored` hides ignored catalog paths (the library already knows them).
ExtractCandidates collectExtractCandidates(const std::vector<SampleRecord>& selection,
                                           const std::function<bool(const juce::String&)>& ignored = {});
} // namespace sonicator
