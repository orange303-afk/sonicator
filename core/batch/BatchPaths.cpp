#include "BatchPaths.h"

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <climits>
 #include <sys/statvfs.h>
 #include <unistd.h>
#endif

namespace sonicator::batch
{
namespace
{
juce::String lowerPath(const juce::File& file) { return file.getFullPathName().toLowerCase(); }

bool containsDotDot(const juce::String& path)
{
    return juce::StringArray::fromTokens(path.replaceCharacter('\\', '/'), "/", "").contains("..");
}

juce::File nearestExistingAncestor(juce::File file)
{
    while (file != file.getParentDirectory() && ! file.exists())
        file = file.getParentDirectory();
    return file;
}

juce::File canonical(const juce::File& file)
{
#if JUCE_WINDOWS
    // Windows has no realpath. getFullPathName already makes the path absolute and native;
    // symbolic-link components are rejected separately by hasSymlinkComponent.
    return juce::File(file.getFullPathName());
#else
    char buffer[PATH_MAX] {};
    if (realpath(file.getFullPathName().toRawUTF8(), buffer) == nullptr) return file;
    return juce::File(juce::String::fromUTF8(buffer));
#endif
}

bool within(const juce::File& parent, const juce::File& child)
{
    // Compare one separator spelling; Windows paths are also case-insensitive.
    auto p = parent.getFullPathName().replaceCharacter('\\', '/');
    auto c = child.getFullPathName().replaceCharacter('\\', '/');
#if JUCE_WINDOWS
    p = p.toLowerCase();
    c = c.toLowerCase();
#endif
    return c == p || c.startsWith(p + "/");
}

int64_t platformFreeBytes(const juce::File& existing)
{
#if JUCE_WINDOWS
    ULARGE_INTEGER available {};
    if (GetDiskFreeSpaceExW(existing.getFullPathName().toWideCharPointer(), &available, nullptr, nullptr) == 0)
        return -1;
    return (int64_t) available.QuadPart;
#else
    struct statvfs info {};
    if (statvfs(existing.getFullPathName().toRawUTF8(), &info) != 0) return -1;
    return (int64_t) info.f_bavail * (int64_t) info.f_frsize;
#endif
}
} // namespace

juce::String rootGroupPrefix(const juce::String& root)
{
    const auto folder = juce::File(root);
    auto name = folder.getFileName();
    if (name.isEmpty()) name = "root";
    const auto hash = juce::SHA256(root.toRawUTF8(), root.getNumBytesAsUTF8()).toHexString().substring(0, 8);
    return name + "-" + hash;
}

juce::File partTarget(const juce::File& planTarget, const juce::String& sourceStem, int index,
                      int count)
{
    int digits = 3;
    for (int value = std::max(1, count); value >= 1000; value /= 10) ++digits;
    auto number = juce::String(std::max(1, index)).paddedLeft('0', digits);
    return planTarget.getSiblingFile(sourceStem + " part " + number
                                     + planTarget.getFileExtension());
}

bool hasSymlinkComponent(const juce::File& file)
{
    auto current = nearestExistingAncestor(file);
    while (current != current.getParentDirectory())
    {
        if (current.isSymbolicLink()) return true;
        current = current.getParentDirectory();
    }
    return false;
}

juce::String unsafeDestinationReason(const juce::File& destinationFolder, const juce::File& target)
{
    if (! juce::File::isAbsolutePath(destinationFolder.getFullPathName())) return "The destination folder must be an absolute path";
    if (containsDotDot(target.getFullPathName())) return "The destination path leaves its folder";
    const auto canonicalFolder = canonical(nearestExistingAncestor(destinationFolder));
    const auto canonicalParent = canonical(nearestExistingAncestor(target.getParentDirectory()));
    if (! within(canonicalFolder, canonicalParent))
        return "The destination resolves outside the chosen folder";
    const auto relative = target.getRelativePathFrom(destinationFolder);
    if (relative.startsWith("..")) return "The destination path leaves its folder";
    // A symlinked directory inside the destination could redirect the write elsewhere.
    auto walk = target.getParentDirectory();
    const auto stop = destinationFolder.getParentDirectory();
    while (walk.getFullPathName().length() > stop.getFullPathName().length()
           && within(destinationFolder, walk))
    {
        if (walk.exists() && walk.isSymbolicLink())
            return "The destination contains a symbolic-link folder";
        walk = walk.getParentDirectory();
    }
    return {};
}

int64_t freeBytesFor(const juce::File& folder, const FreeSpaceProbe& probe)
{
    const auto existing = nearestExistingAncestor(folder);
    if (probe) return probe(existing);
    return platformFreeBytes(existing);
}

PathReserver::PathReserver(const std::vector<juce::String>& protectedIdentities,
                           const std::vector<juce::String>& protectedPaths)
    : identities(protectedIdentities), paths(protectedPaths)
{
    for (auto& path : paths) path = path.toLowerCase();
}

juce::String PathReserver::processedStem(const SourceSnapshot& source)
{
    auto stem = source.path.getFileNameWithoutExtension();
    if (stem.isEmpty()) stem = "audio";
    return stem + " processed";
}

juce::File PathReserver::baseFolder(const SourceSnapshot& source, const Request& request,
                                    const juce::String& groupPrefix)
{
    if (request.destination.nextToSources) return source.path.getParentDirectory();
    auto folder = request.destination.folder;
    if (groupPrefix.isNotEmpty()) folder = folder.getChildFile(groupPrefix);
    if (source.relativeDir.isNotEmpty())
        for (const auto& part : juce::StringArray::fromTokens(source.relativeDir, "/", ""))
            if (part.isNotEmpty() && part != "." && part != "..") folder = folder.getChildFile(part);
    return folder;
}

bool PathReserver::isProtected(const juce::File& candidate) const
{
    const auto lowered = lowerPath(candidate);
    if (std::find(paths.begin(), paths.end(), lowered) != paths.end()) return true;
    if (! candidate.exists()) return false;
    const auto identity = physicalIdentity(candidate);
    return std::find(identities.begin(), identities.end(), identity) != identities.end();
}

bool PathReserver::plan(const SourceSnapshot& source, const Request& request,
                        const juce::String& groupPrefix, Format format, ConflictPolicy conflicts,
                        PlannedPath& planned, Issue& issue)
{
    const auto folder = baseFolder(source, request, groupPrefix);
    if (! request.destination.nextToSources)
    {
        const auto reason = unsafeDestinationReason(folder, folder.getChildFile("probe.tmp"));
        if (reason.isNotEmpty())
        {
            issue = { ErrorCode::unsafe_destination, reason, false, source.path.getFullPathName() };
            return false;
        }
    }
    const auto extension = formatExtension(format);
    auto candidate = folder.getChildFile(processedStem(source) + extension);
    auto suffix = 2;
    auto existsNow = [this](const juce::File& file)
    {
        return file.existsAsFile() || reserved.count(lowerPath(file)) > 0;
    };
    if (conflicts == ConflictPolicy::skip && existsNow(candidate))
    {
        if (isProtected(candidate))
        {
            issue = { ErrorCode::unsafe_destination,
                      "An existing output is the same physical file as a source", false,
                      candidate.getFullPathName() };
            return false;
        }
        planned = { candidate, true, false, {} };
        reserved.insert(lowerPath(candidate));
        return true;
    }
    if (conflicts == ConflictPolicy::replaceOutput
        && reserved.count(lowerPath(candidate)) == 0)
    {
        if (! request.replacementConfirmed)
        {
            issue = { ErrorCode::replacement_not_confirmed,
                      "Replacing existing outputs needs an explicit confirmation", false,
                      candidate.getFullPathName() };
            return false;
        }
        if (isProtected(candidate))
        {
            issue = { ErrorCode::unsafe_destination,
                      "The output would replace a source file or its alias", false,
                      candidate.getFullPathName() };
            return false;
        }
        if (candidate.existsAsFile())
            planned = { candidate, false, true, physicalIdentity(candidate) };
        else
            planned = { candidate, false, false, {} };
        reserved.insert(lowerPath(candidate));
        return true;
    }
    // Rename (default) and Replace for a target already owned by this batch both reserve the first
    // free name. A same-stem sibling never loses its planned output.
    while (existsNow(candidate))
    {
        candidate = folder.getChildFile(processedStem(source) + " " + juce::String(suffix++)
                                                      + extension);
    }
    if (isProtected(candidate))
    {
        issue = { ErrorCode::unsafe_destination,
                  "The planned output is the same physical file as a source", false,
                  candidate.getFullPathName() };
        return false;
    }
    planned = { candidate, false, false, {} };
    reserved.insert(lowerPath(candidate));
    return true;
}
} // namespace sonicator::batch
