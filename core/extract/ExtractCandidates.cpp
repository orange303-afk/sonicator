#include "ExtractCandidates.h"
#include "EmbeddedSamples.h"
#include <map>
#include <set>
namespace sonicator {
ExtractRouting routeExtractSources(const std::vector<juce::File>& files,
                                   const std::vector<DiscExtractJob>& jobs)
{
    ExtractRouting routing;
    std::set<juce::String> jobImages, embeddedPaths;
    for (const auto& job : jobs)
    {
        if (job.image == juce::File()) continue;
        if (! jobImages.insert(job.image.getFullPathName()).second) continue;
        routing.jobs.push_back(job);
    }
    for (const auto& file : files)
    {
        if (! file.existsAsFile()) continue;
        const auto path = file.getFullPathName();
        if (isDiscExtractSource(file))
        {
            if (jobImages.count(path) > 0) continue; // selected records already cover it
            if (! jobImages.insert(path).second) continue;
            routing.jobs.push_back({ file, {} });
        }
        else if (isEmbeddedSampleSource(file) && embeddedPaths.insert(path).second)
        {
            routing.embedded.push_back(file);
        }
    }
    return routing;
}

ExtractCandidates collectExtractCandidates(const std::vector<SampleRecord>& selection,
                                           const std::function<bool(const juce::String&)>& ignored)
{
    ExtractCandidates result;
    std::set<juce::String> seenFiles;
    std::map<juce::String, size_t> jobByImage;
    for (const auto& record : selection)
    {
        if (ignored && ignored(record.filePath)) continue;
        if (record.isDiscEntry())
        {
            const auto id = record.discEntry.getLargeIntValue();
            if (id <= 0) continue;
            auto found = jobByImage.find(record.containerPath);
            if (found == jobByImage.end())
            {
                found = jobByImage.emplace(record.containerPath,
                                           result.entries.size()).first;
                result.entries.push_back({ juce::File(record.containerPath), {} });
            }
            DiscExtractEntry entry;
            entry.id = { (uint64_t) id };
            const auto partner = record.discPartner.getLargeIntValue();
            if (partner > 0) entry.partner = { (uint64_t) partner };
            entry.name = record.fileName;
            result.entries[found->second].entries.push_back(entry);
            continue;
        }
        const juce::File file(record.filePath);
        if (! file.existsAsFile()) continue;
        if (! seenFiles.insert(file.getFullPathName()).second) continue;
        if (isDiscExtractSource(file)) result.images.push_back(file);
        else if (isEmbeddedSampleSource(file)) result.embedded.push_back(file);
    }
    return result;
}
} // namespace sonicator
