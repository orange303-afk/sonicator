#include "BatchSources.h"
#include "BatchPaths.h"
#include "BatchSplit.h"
#include "core/audio/SonicatorFormats.h"
#include <algorithm>
#include <set>
#include <unordered_set>

namespace sonicator::batch
{
namespace
{
bool cancelled(const CancelCheck& check) { return check && check(); }

juce::File canonicalFile(const juce::File& file) { return juce::File(file.getFullPathName()); }

juce::String relativeToRoot(const juce::String& path, const juce::String& root)
{
    if (root.isEmpty()) return {};
    auto relative = juce::File(path).getRelativePathFrom(juce::File(root));
    if (relative == "." || relative.startsWith("..")) return {};
    return relative.replaceCharacter('\\', '/');
}

bool isLossyExtension(const juce::File& file)
{
    return file.hasFileExtension("mp3;ogg");
}

void addSkipped(PreflightResult& result, ErrorCode code, const juce::String& message,
                const juce::String& path, bool warning = false)
{
    result.issues.push_back({ code, message, warning, path });
    result.batch.skipped.push_back({ code, message, warning, path });
}
} // namespace

juce::String registeredRootFor(const juce::String& path, const juce::StringArray& roots)
{
    auto normalized = canonicalFile(juce::File(path)).getFullPathName().replaceCharacter('\\', '/');
    juce::String best;
    for (const auto& root : roots)
    {
        auto clean = canonicalFile(juce::File(root)).getFullPathName().replaceCharacter('\\', '/');
        if (clean.isEmpty()) continue;
        if (normalized == clean || normalized.startsWith(clean + "/"))
            if (clean.length() > best.length()) best = clean;
    }
    return best;
}

SourceProbe probeSource(const juce::File& file, int64_t sampleId, const juce::String& root,
                        const juce::String& layoutBase, CancelCheck cancel)
{
    SourceProbe probe;
    if (cancelled(cancel))
    {
        probe.code = ErrorCode::cancelled;
        probe.message = "Cancelled";
        return probe;
    }
    const auto path = file.getFullPathName();
    if (! file.existsAsFile())
    {
        probe.code = ErrorCode::missing_source;
        probe.message = "The file is missing";
        return probe;
    }
    if (file.isSymbolicLink())
    {
        probe.code = ErrorCode::symlink_source_unsupported;
        probe.message = "Symbolic links are not supported as batch inputs";
        return probe;
    }
    if (! FileScanner::isSupportedFormat(file) || FileScanner::isMetadataOnlyFormat(file))
    {
        probe.code = ErrorCode::unsupported_source;
        probe.message = "This is not a supported single audio file";
        return probe;
    }
    if (file.getSize() <= 0)
    {
        probe.code = ErrorCode::empty_source;
        probe.message = "The file is empty";
        return probe;
    }
    {
        auto input = file.createInputStream();
        if (input == nullptr)
        {
            probe.code = ErrorCode::unsupported_source;
            probe.message = "The file cannot be read";
            return probe;
        }
    }
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        probe.code = ErrorCode::decode_failed;
        probe.message = "No audio reader can decode this file";
        return probe;
    }
    if (reader->lengthInSamples <= 0)
    {
        probe.code = ErrorCode::empty_source;
        probe.message = "The file contains no audio frames";
        return probe;
    }
    if (reader->numChannels < 1 || reader->numChannels > 2)
    {
        probe.code = ErrorCode::unsupported_channels;
        probe.message = juce::String((int) reader->numChannels)
            + " channels are not supported; only mono and stereo are";
        return probe;
    }
    if (! std::isfinite(reader->sampleRate) || reader->sampleRate <= 0
        || std::abs(reader->sampleRate - std::round(reader->sampleRate)) > 1.0e-6)
    {
        probe.code = ErrorCode::unsupported_sample_rate;
        probe.message = "The sample rate is invalid";
        return probe;
    }
    SourceSnapshot snapshot;
    snapshot.sampleId = sampleId;
    snapshot.path = file;
    snapshot.root = root;
    snapshot.relativeDir = relativeToRoot(path, layoutBase);
    const auto slash = juce::jmax(snapshot.relativeDir.lastIndexOfChar('/'),
                                  snapshot.relativeDir.lastIndexOfChar('\\'));
    if (slash >= 0)
        snapshot.relativeDir = snapshot.relativeDir.substring(0, slash);
    else
        snapshot.relativeDir.clear();
    snapshot.identity = physicalIdentity(file);
    snapshot.fileSize = file.getSize();
    snapshot.modifiedMs = file.getLastModificationTime().toMilliseconds();
    snapshot.sampleRate = (int) std::lround(reader->sampleRate);
    snapshot.channels = (int) reader->numChannels;
    snapshot.bits = isLossyExtension(file) ? 0 : (int) reader->bitsPerSample;
    snapshot.floating = reader->usesFloatingPointData;
    snapshot.frames = reader->lengthInSamples;
    probe.ok = true;
    probe.hasSnapshot = true;
    probe.snapshot = std::move(snapshot);
    return probe;
}


namespace
{
// Plans outputs for an already snapshotted batch. Shared by the first preflight and by Start,
// where re-probing every source would freeze the message thread.
bool planOutputs(PreflightResult& result, const Request& request, CancelCheck cancel)
{
    auto fail = [&result](ErrorCode code, const juce::String& message)
    {
        result.ok = false;
        result.issues.push_back({ code, message, false, {} });
        result.batch.skipped.push_back({ code, message, false, {} });
        return false;
    };
    std::set<juce::String> baseRoots;
    for (const auto& source : result.batch.sources)
        baseRoots.insert(source.root.isNotEmpty() ? source.root
                                                  : source.path.getParentDirectory().getFullPathName());
    const bool multipleRoots = baseRoots.size() > 1;

    PathReserver reserver(result.batch.protectedIdentities, result.batch.protectedPaths);
    const auto group = [&multipleRoots](const SourceSnapshot& source)
    {
        if (! multipleRoots) return juce::String();
        return rootGroupPrefix(source.root.isNotEmpty()
                                   ? source.root
                                   : source.path.getParentDirectory().getFullPathName());
    };

    int outputsRejected = 0;
    for (size_t index = 0; index < result.batch.sources.size(); ++index)
    {
        const auto& source = result.batch.sources[index];
        for (const auto& preset : request.recipe.outputs)
        {
            if (cancelled(cancel)) return fail(ErrorCode::cancelled, "Cancelled");
            const auto outputPrefix = source.path.getFileName() + " -> " + formatLabel(preset.format);
            Encoding encoding = preset.encoding;
            int bits = 0;
            bool floating = false;
            juce::String reason;
            if (! resolveEncoding(preset, source, encoding, bits, floating, reason))
            {
                ++outputsRejected;
                result.issues.push_back({ ErrorCode::invalid_encoding,
                                          outputPrefix + ": " + reason, false,
                                          source.path.getFullPathName() });
                continue;
            }
            OutputPlan plan;
            plan.outputIndex = result.batch.outputs.size();
            plan.sourceIndex = index;
            plan.format = preset.format;
            plan.encoding = encoding;
            plan.bits = bits;
            plan.floating = floating;
            plan.mp3Mode = preset.mp3Mode;
            plan.mp3BitrateKbps = preset.mp3BitrateKbps;
            plan.mp3VbrQuality = preset.mp3VbrQuality;
            plan.oggQuality = preset.oggQuality;
            plan.sampleRate = request.recipe.sampleRate > 0 ? request.recipe.sampleRate
                                                           : source.sampleRate;
            switch (request.recipe.channels)
            {
                case ChannelMode::sameAsSource: plan.channels = source.channels; break;
                case ChannelMode::mono: plan.channels = 1; break;
                case ChannelMode::stereo: plan.channels = 2; break;
            }
            plan.estimatedFrames = outputFrameEstimate(source.frames, source.sampleRate,
                                                       plan.sampleRate);
            plan.estimatedBytes = outputByteEstimate(plan.estimatedFrames, plan.channels, bits);
            if (preset.format == Format::mp3 || preset.format == Format::ogg)
            {
                const int kbps = preset.format == Format::mp3
                    ? (preset.mp3Mode == Mp3Mode::cbr ? preset.mp3BitrateKbps : 245)
                    : 64 + preset.oggQuality * 24;
                plan.estimatedBytes = plan.estimatedFrames / std::max(1, plan.sampleRate)
                    * (int64_t) kbps * 1000 / 8 + 8192;
            }
            constexpr int64_t flacFrameLimit = 0x1000000000LL; // 2^36 samples per stream
            if (preset.format == Format::flac && plan.estimatedFrames >= flacFrameLimit)
            {
                ++outputsRejected;
                result.issues.push_back({ ErrorCode::container_limit,
                                          outputPrefix + ": the file would exceed the FLAC frame limit",
                                          false, source.path.getFullPathName() });
                continue;
            }
            if ((preset.format == Format::wav || preset.format == Format::aiff)
                && plan.estimatedBytes >= containerByteLimit)
            {
                ++outputsRejected;
                result.issues.push_back({ ErrorCode::container_limit,
                                          outputPrefix + ": the file would exceed the 4 GiB container limit",
                                          false, source.path.getFullPathName() });
                continue;
            }
            PlannedPath planned;
            Issue pathIssue;
            if (! reserver.plan(source, request, group(source), preset.format,
                                request.conflicts, planned, pathIssue))
            {
                ++outputsRejected;
                result.issues.push_back(pathIssue);
                continue;
            }
            plan.target = planned.target;
            plan.skipExisting = recipeHasSplit(request.recipe) ? false : planned.skipExisting;
            plan.replaceExisting = planned.replaceExisting;
            plan.previousFingerprint = planned.previousFingerprint;
            result.batch.outputs.push_back(plan);
        }
    }
    if (result.batch.outputs.empty())
    {
        if (outputsRejected > 0) return fail(ErrorCode::no_outputs, "No output format can be written");
        return fail(ErrorCode::no_outputs, "Select at least one output format");
    }

    for (const auto& plan : result.batch.outputs) result.batch.estimatedBytes += plan.estimatedBytes;
    if (! result.batch.outputs.empty())
    {
        const auto target = PathReserver::baseFolder(result.batch.sources[result.batch.outputs[0].sourceIndex],
                                                     request, group(result.batch.sources[result.batch.outputs[0].sourceIndex]));
        result.batch.freeBytes = freeBytesFor(target);
        result.batch.freeSpaceKnown = result.batch.freeBytes >= 0;
        if (! result.batch.freeSpaceKnown)
            result.issues.push_back({ ErrorCode::unknown_free_space,
                                      "Free disk space could not be checked", true,
                                      target.getFullPathName() });
        else if (result.batch.freeBytes < result.batch.estimatedBytes + spaceReserveBytes)
            return fail(ErrorCode::insufficient_space,
                        "Not enough free space for the planned outputs");
    }
    result.ok = true;
    return true;
}

} // namespace

PreflightResult prepareBatch(SampleRepository& repo, const Request& request, CancelCheck cancel)
{
    PreflightResult result;
    result.batch.request = request;
    result.batch.jobId = juce::Uuid().toDashedString();
    auto fail = [&result](ErrorCode code, const juce::String& message)
    {
        result.issues.push_back({ code, message, false, {} });
        result.batch.skipped.push_back({ code, message, false, {} });
        result.ok = false;
        return result;
    };
    if (cancelled(cancel)) return fail(ErrorCode::cancelled, "Cancelled");
    const auto recipeIssues = validateRecipe(request.recipe);
    bool recipeFatal = false;
    for (auto issue : recipeIssues)
    {
        if (! issue.warning) recipeFatal = true;
        result.issues.push_back(issue);
    }
    if (recipeFatal) return fail(ErrorCode::invalid_recipe, "The processing settings are invalid");
    if (recipeHasSplit(request.recipe) && request.conflicts == ConflictPolicy::replaceOutput)
        return fail(ErrorCode::invalid_recipe,
                    "Replace cannot be used together with Split; use Rename or Skip");

    const auto roots = repo.roots();
    struct Candidate { juce::File file; int64_t sampleId; };
    std::vector<Candidate> candidates;
    std::vector<juce::File> requestedFiles;
    std::unordered_set<std::string> seen;
    auto wanted = [&request](int64_t id)
    {
        return std::find(request.sources.unsavedEditedRows.begin(),
                         request.sources.unsavedEditedRows.end(), id)
            != request.sources.unsavedEditedRows.end();
    };
    auto push = [&candidates, &seen](const juce::File& file, int64_t id)
    {
        const auto key = file.getFullPathName().toStdString();
        if (seen.insert(key).second) candidates.push_back({ file, id });
    };

    if (request.sources.kind == SourceSelection::Kind::rows)
    {
        auto ids = request.sources.rows;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        auto records = repo.recordsByIds(ids);
        auto partners = repo.stereoPairPartners(ids);
        for (const auto& record : records)
        {
            if (cancelled(cancel)) return fail(ErrorCode::cancelled, "Cancelled");
            requestedFiles.emplace_back(record.filePath);
            if (record.isDiscEntry())
            {
                addSkipped(result, ErrorCode::unsupported_source,
                           "Audio inside a container is not a batch source", record.filePath);
                continue;
            }
            if (repo.isIgnored(record.filePath))
            {
                addSkipped(result, ErrorCode::unsupported_source,
                           "The file is ignored in this catalog", record.filePath);
                continue;
            }
            if (wanted(record.id))
            {
                addSkipped(result, ErrorCode::unsaved_editor_changes,
                           "The file has unsaved editor changes", record.filePath);
                continue;
            }
            push(juce::File(record.filePath), record.id);
            if (auto found = partners.find(record.id); found != partners.end())
            {
                const auto partner = juce::File(found->second);
                if (! partner.existsAsFile())
                {
                    addSkipped(result, ErrorCode::stereo_partner_missing,
                               "The stereo partner is missing and was skipped",
                               partner.getFullPathName(), true);
                    continue;
                }
                int64_t partnerId = 0;
                if (auto partnerRecord = repo.findByPath(partner.getFullPathName()))
                    partnerId = partnerRecord->id;
                push(partner, partnerId);
            }
        }
    }
    else
    {
        const auto folder = juce::File(request.sources.folder);
        if (folder.getFullPathName().isEmpty() || ! folder.isDirectory())
            return fail(ErrorCode::folder_not_registered,
                        "Only a catalogued physical folder can be batch processed");
        const auto root = registeredRootFor(folder.getFullPathName(), roots);
        if (root.isEmpty())
            return fail(ErrorCode::folder_not_registered,
                        "The folder is not inside a registered library root");
        const auto entries = repo.physicalPathsInFolder(folder.getFullPathName(), maxSources + 1);
        if ((int) entries.size() > maxSources)
            return fail(ErrorCode::too_many_sources,
                        "More than " + juce::String(maxSources)
                            + " files were found; choose a smaller folder");
        const auto ignored = repo.ignoredPaths();
        for (const auto& entry : entries)
        {
            if (cancelled(cancel)) return fail(ErrorCode::cancelled, "Cancelled");
            requestedFiles.emplace_back(entry.path);
            if (entry.containerPath.isNotEmpty()) continue;
            if (isPathIgnored(entry.path, ignored))
            {
                addSkipped(result, ErrorCode::unsupported_source,
                           "The file is ignored in this catalog", entry.path);
                continue;
            }
            if (entry.missing)
            {
                addSkipped(result, ErrorCode::missing_source,
                           "The catalogued file is missing", entry.path);
                continue;
            }
            if (wanted(entry.id))
            {
                addSkipped(result, ErrorCode::unsaved_editor_changes,
                           "The file has unsaved editor changes", entry.path);
                continue;
            }
            push(juce::File(entry.path), entry.id);
        }
    }

    for (auto& candidate : candidates)
    {
        if (cancelled(cancel)) return fail(ErrorCode::cancelled, "Cancelled");
        const auto root = registeredRootFor(candidate.file.getFullPathName(), roots);
        const auto layoutBase = request.sources.kind == SourceSelection::Kind::folder
            ? juce::File(request.sources.folder).getFullPathName()
            : (root.isNotEmpty() ? root : candidate.file.getParentDirectory().getFullPathName());
        auto probe = probeSource(candidate.file, candidate.sampleId, root, layoutBase, cancel);
        if (! probe.ok)
        {
            addSkipped(result, probe.code, probe.message, candidate.file.getFullPathName());
            continue;
        }
        result.batch.sources.push_back(std::move(probe.snapshot));
    }
    if (result.batch.sources.empty())
        return fail(ErrorCode::no_sources, "No file can be processed with the current selection");

    std::sort(result.batch.sources.begin(), result.batch.sources.end(),
              [](const SourceSnapshot& a, const SourceSnapshot& b)
              {
                  return a.path.getFullPathName().compareIgnoreCase(b.path.getFullPathName()) < 0;
              });

    // Every candidate in the request is protected, including inputs that were skipped (ignored,
    // unsupported channels, unsaved editor rows). Replace must never turn a skipped input into an
    // output target.
    for (const auto& file : requestedFiles)
    {
        if (! file.exists()) continue;
        result.batch.protectedIdentities.push_back(physicalIdentity(file));
        result.batch.protectedPaths.push_back(file.getFullPathName());
    }
    for (const auto& candidate : candidates)
    {
        if (! candidate.file.exists()) continue;
        result.batch.protectedIdentities.push_back(physicalIdentity(candidate.file));
        result.batch.protectedPaths.push_back(candidate.file.getFullPathName());
    }
    for (const auto& source : result.batch.sources)
    {
        result.batch.protectedIdentities.push_back(source.identity);
        result.batch.protectedPaths.push_back(source.path.getFullPathName());
    }
    if (! planOutputs(result, request, cancel)) return result;
    result.ok = true;
    return result;
}
PreflightResult replanBatch(const PreparedBatch& base, const Request& request, CancelCheck cancel)
{
    PreflightResult result;
    result.batch = base;
    result.batch.request = request;
    result.batch.jobId = juce::Uuid().toDashedString();
    result.batch.outputs.clear();
    result.batch.estimatedBytes = 0;
    result.issues.clear();
    result.ok = true;
    if (base.sources.empty())
    {
        result.ok = false;
        result.issues.push_back({ ErrorCode::no_sources, "No eligible source file", false, {} });
        return result;
    }
    const auto recipeIssues = validateRecipe(request.recipe);
    bool fatal = false;
    for (const auto& issue : recipeIssues)
    {
        result.issues.push_back(issue);
        if (! issue.warning) fatal = true;
    }
    if (fatal) { result.ok = false; return result; }
    if (! planOutputs(result, request, cancel)) return result;
    result.ok = true;
    return result;
}
} // namespace sonicator::batch
