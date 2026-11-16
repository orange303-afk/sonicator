#pragma once
#include "BatchTypes.h"
#include <functional>
#include <set>

namespace sonicator::batch
{
struct PlannedPath
{
    juce::File target;
    bool skipExisting = false;
    bool replaceExisting = false;
    juce::String previousFingerprint;
};

// Plans every output path before any audio is written. Names are reserved case-insensitively in
// one deterministic pass, so two sources with the same stem never overwrite each other.
class PathReserver
{
public:
    PathReserver(const std::vector<juce::String>& protectedIdentities,
                 const std::vector<juce::String>& protectedPaths);
    // False with a fatal issue when the destination escapes the chosen folder or aliases a source.
    bool plan(const SourceSnapshot&, const Request&, const juce::String& groupPrefix,
              Format, ConflictPolicy, PlannedPath&, Issue&);
    static juce::File baseFolder(const SourceSnapshot&, const Request&, const juce::String& groupPrefix);
    static juce::String processedStem(const SourceSnapshot&);
private:
    bool isProtected(const juce::File& candidate) const;
    std::vector<juce::String> identities, paths;
    std::set<juce::String> reserved;
};

// Group prefix for a batch that draws from more than one registered root: root name plus a stable
// hash of the full root path. Empty when only one base applies.
juce::String rootGroupPrefix(const juce::String& root);
// `<stem> part 001.<ext>` for a split part; three digits minimum.
juce::File partTarget(const juce::File& planTarget, const juce::String& sourceStem, int index,
                      int count);
// Empty on success; otherwise a user-facing reason.
juce::String unsafeDestinationReason(const juce::File& destinationFolder, const juce::File& target);
// Reports bytes free on the volume that holds `folder`, probing its nearest existing ancestor.
// -1 when the platform cannot report free space. An empty `probe` uses the platform query; tests
// pass a probe to fake a full or unknown volume without filling a real disk.
using FreeSpaceProbe = std::function<int64_t (const juce::File& existingAncestor)>;
int64_t freeBytesFor(const juce::File& folder, const FreeSpaceProbe& probe = {});
// True when the path itself or any existing ancestor is a symbolic link.
bool hasSymlinkComponent(const juce::File& file);
} // namespace sonicator::batch
