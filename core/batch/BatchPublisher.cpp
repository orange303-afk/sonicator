#include "BatchPublisher.h"
#include <cerrno>
#include <cstring>

#if JUCE_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sonicator::batch
{
InputProtectionSet InputProtectionSet::from(const PreparedBatch& prepared)
{
    InputProtectionSet set;
    set.identities = prepared.protectedIdentities;
    for (const auto& path : prepared.protectedPaths) set.paths.push_back(path.toLowerCase());
    return set;
}

bool InputProtectionSet::protects(const juce::File& file) const
{
    if (std::find(paths.begin(), paths.end(), file.getFullPathName().toLowerCase()) != paths.end())
        return true;
    if (! file.exists()) return false;
    return std::find(identities.begin(), identities.end(), physicalIdentity(file)) != identities.end();
}

namespace
{
juce::File nextRenameTarget(const juce::File& base, int suffix)
{
    if (suffix <= 1) return base;
    return base.getSiblingFile(base.getFileNameWithoutExtension() + " " + juce::String(suffix)
                               + base.getFileExtension());
}

bool sameIdentity(const juce::File& file, const juce::String& identity)
{
    return file.exists() && physicalIdentity(file) == identity;
}
} // namespace

juce::File createBatchTemporary(const juce::File& target)
{
    const auto name = "." + target.getFileNameWithoutExtension() + "."
        + juce::Uuid().toDashedString() + ".batchtmp" + target.getFileExtension();
    return target.getSiblingFile(name);
}

PublishResult publishBatchFile(const juce::File& temporary, const OutputPlan& plan,
                               ConflictPolicy policy, const InputProtectionSet& protection,
                               const SourceSnapshot& source, CancelCheck cancel)
{
    if (cancel && cancel()) return { false, {}, ErrorCode::cancelled, "Cancelled" };
    if (! temporary.existsAsFile())
        return { false, {}, ErrorCode::publish_failed, "The temporary file is missing" };
    if (! source.path.existsAsFile() || physicalIdentity(source.path) != source.identity)
        return { false, {}, ErrorCode::source_changed,
                 "The source changed before publication" };
    auto target = plan.target;
    if (protection.protects(target))
        return { false, {}, ErrorCode::unsafe_destination,
                 "The output path is a source or an alias of a source" };

    if (policy == ConflictPolicy::replaceOutput)
    {
        if (plan.replaceExisting)
        {
            if (! target.exists())
                return { false, {}, ErrorCode::destination_changed,
                         "The previous output no longer exists" };
            if (target.isDirectory())
                return { false, {}, ErrorCode::unsafe_destination, "The output path is a folder" };
            if (plan.previousFingerprint.isNotEmpty()
                && ! sameIdentity(target, plan.previousFingerprint))
                return { false, {}, ErrorCode::destination_changed,
                         "The previous output changed after preflight" };
        }
        else if (target.exists())
        {
            // A file appeared after preflight: never replace something the person did not see.
            return { false, {}, ErrorCode::destination_changed,
                     "An output appeared after preflight and was not replaced" };
        }
#if JUCE_WINDOWS
        if (! MoveFileExW(temporary.getFullPathName().toWideCharPointer(),
                          target.getFullPathName().toWideCharPointer(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return { false, {}, ErrorCode::publish_failed, "The output could not be replaced" };
#else
        if (::rename(temporary.getFullPathName().toRawUTF8(),
                     target.getFullPathName().toRawUTF8()) != 0)
            return { false, {}, ErrorCode::publish_failed,
                     juce::String("The output could not be replaced: ") + juce::String(std::strerror(errno)) };
#endif
        return { true, target, ErrorCode::none, {} };
    }

    // Rename (and Skip never reaches here): no-clobber publication. link() is atomic and fails
    // with EEXIST, so an external file created after preflight is never overwritten.
    for (int suffix = 1; suffix < 100000; ++suffix)
    {
        auto candidate = nextRenameTarget(plan.target, suffix);
        if (candidate.exists() || protection.protects(candidate))
        {
            if (protection.protects(candidate))
                return { false, {}, ErrorCode::unsafe_destination,
                         "The output would replace an input or its alias" };
            continue;
        }
#if JUCE_WINDOWS
        if (MoveFileExW(temporary.getFullPathName().toWideCharPointer(),
                        candidate.getFullPathName().toWideCharPointer(), 0))
            return { true, candidate, ErrorCode::none, {} };
        if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS)
            continue;
        return { false, {}, ErrorCode::publish_failed, "The output could not be published" };
#else
        if (::link(temporary.getFullPathName().toRawUTF8(),
                   candidate.getFullPathName().toRawUTF8()) == 0)
        {
            if (::unlink(temporary.getFullPathName().toRawUTF8()) != 0)
            {
                // The file is published; the extra temporary name is harmless and reported.
                return { true, candidate, ErrorCode::none, {} };
            }
            return { true, candidate, ErrorCode::none, {} };
        }
        if (errno == EEXIST) continue;
        return { false, {}, ErrorCode::publish_failed,
                 juce::String("The output could not be published: ") + juce::String(std::strerror(errno)) };
#endif
    }
    return { false, {}, ErrorCode::publish_failed, "No free output name was found" };
}
} // namespace sonicator::batch
