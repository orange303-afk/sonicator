#include "FolderIndex.h"
#include "core/database/AssetCompatibility.h"
#include "core/disc/DiscContentIndexer.h"

namespace sonicator
{
namespace
{
// Paths come from one filesystem, so a trailing separator is the only difference a root can have.
juce::String withoutTrailingSeparator(juce::String path)
{
    while (path.length() > 1 && (path.endsWithChar('/') || path.endsWithChar('\\')))
        path = path.dropLastCharacters(1);
    return path;
}

// True when `path` is the folder `root` itself or something inside it. A plain prefix test would
// treat "/a/bc" as a child of "/a/b", so the character after the root must be a separator.
bool isUnder(const juce::String& path, const juce::String& root)
{
    const auto base = withoutTrailingSeparator(root);
    if (path == base) return true;
    if (path.length() <= base.length() || ! path.startsWith(base)) return false;
    const auto next = path[base.length()];
    return next == '/' || next == '\\';
}

bool isUnderAny(const juce::String& path, const juce::StringArray& roots)
{
    for (const auto& root : roots)
        if (isUnder(path, root)) return true;
    return false;
}

juce::String parentFolderOf(const juce::String& path)
{
    const auto slash = juce::jmax(path.lastIndexOfChar('/'), path.lastIndexOfChar('\\'));
    return slash <= 0 ? juce::String() : path.substring(0, slash);
}

// The catalog stores paths, not File objects: comparing suffixes keeps 300 000 rows from building
// a File each. Only a real .bin/.img stays a File, because the sibling cue lookup reads the disk.
bool hasImageExtension(const juce::String& path)
{
    for (const auto* extension : { ".iso", ".bin", ".mdx", ".cue", ".akai", ".img" })
        if (path.endsWithIgnoreCase(extension)) return true;
    return false;
}
bool isBinOrImg(const juce::String& path)
{
    return path.endsWithIgnoreCase(".bin") || path.endsWithIgnoreCase(".img");
}
} // namespace

FolderIndex buildFolderIndex(SampleRepository& repo, const FolderIndexRequest& request,
                             const std::function<bool()>& cancelled)
{
    FolderIndex index;
    index.roots = request.roots;
    if (request.showAllSamples && ! request.revealIgnored)
        for (int i = index.roots.size(); --i >= 0;)
            if (isPathIgnored(index.roots[i], request.ignored)) index.roots.remove(i);

    std::set<juce::String> visitedFolders;
    int64_t last = 0;
    for (;;)
    {
        if (cancelled && cancelled()) return {};
        auto rows = repo.pathsAfterId(last, 5000);
        if (rows.empty()) break;
        last = rows.back().id;

        std::vector<int64_t> imageIds;
        for (const auto& row : rows)
        {
            if (row.containerPath.isNotEmpty()) continue;   // disc contents stay inside the image
            if (! request.revealIgnored && isPathIgnored(row.path, request.ignored)) continue;

            auto folder = parentFolderOf(row.path);
            // A .bin/.img with a sibling .cue is the same image; show the cue only, once.
            const bool representedByCue = isBinOrImg(row.path)
                && ! isDiscContentCandidate(juce::File(row.path));
            if (! representedByCue && hasImageExtension(row.path))
            {
                index.images.insert(row.path);
                imageIds.push_back(row.id);
                index.children[folder].insert(row.path);
            }

            // Most files share folders. Build each ancestor chain once, not once per audio file.
            if (! visitedFolders.insert(folder).second) continue;
            for (const auto& root : index.roots)
            {
                const auto boundary = withoutTrailingSeparator(root);
                if (! isUnder(folder, boundary)) continue;
                while (folder != boundary)
                {
                    const auto parent = parentFolderOf(folder);
                    if (parent.isEmpty()) break;
                    index.children[parent].insert(folder);
                    folder = parent;
                }
                break;
            }
        }

        for (const auto& image : repo.recordsByIds(imageIds))
            if (isIncompatible(image.compatibility) || image.error.isNotEmpty())
                index.failedImages.insert(image.filePath);
    }
    return index;
}

FolderIndex folderIndexUnderRoots(const FolderIndex& source, const juce::StringArray& roots)
{
    FolderIndex index;
    index.roots = roots;
    for (const auto& [parent, children] : source.children)
    {
        if (! isUnderAny(parent, roots)) continue;
        auto& target = index.children[parent];
        for (const auto& child : children)
            if (isUnderAny(child, roots)) target.insert(child);
    }
    for (const auto& image : source.images)
        if (isUnderAny(image, roots)) index.images.insert(image);
    for (const auto& image : source.failedImages)
        if (isUnderAny(image, roots)) index.failedImages.insert(image);
    return index;
}
} // namespace sonicator
