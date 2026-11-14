#pragma once
#include "core/database/SampleRepository.h"
#include <functional>
#include <map>
#include <set>

namespace sonicator
{
// The folder structure the library tree shows: the roots that carry content, the child folders of
// each folder, and the disc images and failed images found among the files.
struct FolderIndex
{
    juce::StringArray roots;
    std::map<juce::String, std::set<juce::String>> children;
    std::set<juce::String> images;
    std::set<juce::String> failedImages;
};

struct FolderIndexRequest
{
    juce::StringArray roots;
    std::vector<IgnoredPath> ignored;
    bool revealIgnored = false;
    bool showAllSamples = true;
};

// Streams the catalog and groups its paths into folders. The repository is only read, so this may
// run on a worker thread. `cancelled` is polled between pages and returns an empty index when it
// fires. A root under an ignored folder is dropped unless ignored entries are revealed.
FolderIndex buildFolderIndex(SampleRepository&, const FolderIndexRequest&,
                             const std::function<bool()>& cancelled = {});

// Copies the part of an index under `roots`: folder subtrees, their images and their failures.
FolderIndex folderIndexUnderRoots(const FolderIndex&, const juce::StringArray& roots);
}
