#include "DiscContentIndexer.h"
#include <map>
#include <set>
namespace sonicator {
namespace {
bool audioName(const juce::String& name)
{
    return name.endsWithIgnoreCase(".wav") || name.endsWithIgnoreCase(".aif")
        || name.endsWithIgnoreCase(".aiff") || name.endsWithIgnoreCase(".flac")
        || name.endsWithIgnoreCase(".mp3") || name.endsWithIgnoreCase(".ogg");
}
bool waitFor(DiscBrowserService& service, DiscBrowserService::Kind kind,
             DiscBrowserService::Completion& completion, const CancelCheck& cancel)
{
    for (;;)
    {
        if (cancel && cancel())
        {
            service.close();
            return false;
        }
        DiscBrowserService::Completion next;
        if (service.poll(next))
        {
            if (next.kind == kind) { completion = std::move(next); return true; }
            continue;
        }
        juce::Thread::sleep(2);
    }
}
juce::String internalPath(const std::map<DiscEntryId, const DiscEntry*>& byId, const DiscEntry& entry,
                          DiscEntryId root)
{
    juce::StringArray parts { entry.displayName };
    std::set<DiscEntryId> seen;
    auto parent = entry.parent;
    for (int depth = 0; depth < 256 && parent != root && seen.insert(parent).second; ++depth)
    {
        auto found = byId.find(parent);
        if (found == byId.end() || found->second->displayName.isEmpty()) break;
        parts.insert(0, found->second->displayName);
        parent = found->second->parent;
    }
    return "/" + parts.joinIntoString("/");
}
SampleRecord makeRow(const juce::File& image, const DiscEntry& entry, const juce::String& path,
                     bool akai)
{
    SampleRecord row;
    row.filePath = image.getFullPathName() + "::" + path;
    row.fileName = entry.displayName;
    const auto extension = entry.displayName.fromLastOccurrenceOf(".", false, false);
    row.format = akai ? juce::String("AKAI")
                      : (extension.isNotEmpty() ? extension.toUpperCase() : juce::String("AUDIO"));
    row.fileSize = (int64_t) entry.declaredBytes;
    row.dateModified = image.getLastModificationTime().toMilliseconds();
    row.dateAnalyzed = juce::Time::currentTimeMillis();
    row.duration = 0; row.sampleRate = 0; row.bitsPerSample = 0; row.numChannels = 0;
    row.version = metadataOnlyVersion;
    row.peakLevel = 0; row.rmsLevel = 0;
    row.compatibility = AssetCompatibility::supported;
    row.containerPath = image.getFullPathName();
    row.discEntry = juce::String(entry.id.value);
    row.discParent = juce::String(entry.parent.value);
    row.discPartner = entry.partner.value != 0 ? juce::String(entry.partner.value) : juce::String();
    row.discPath = path;
    return row;
}
juce::File cueDataFile(const juce::File& cue)
{
    juce::FileInputStream input(cue);
    if (! input.openedOk()) return {};
    juce::MemoryBlock block;
    const auto bytes = (size_t) juce::jlimit<juce::int64>(0, 1 << 20, input.getTotalLength());
    if (input.readIntoMemoryBlock(block, bytes) == 0) return {};
    const auto text = block.toString();
    const auto fileAt = text.indexOfIgnoreCase("FILE");
    if (fileAt < 0) return {};
    const auto firstQuote = text.indexOfChar(fileAt, '"');
    if (firstQuote < 0) return {};
    const auto secondQuote = text.indexOfChar(firstQuote + 1, '"');
    if (secondQuote <= firstQuote) return {};
    auto name = text.substring(firstQuote + 1, secondQuote).trim();
    if (name.isEmpty()) return {};
    return juce::File::isAbsolutePath(name) ? juce::File(name) : cue.getSiblingFile(name);
}
}
DiscImageStamp discImageStamp(const juce::File& file)
{
    DiscImageStamp stamp;
    if (! file.existsAsFile()) return stamp;
    stamp.ok = true;
    stamp.size = file.getSize();
    stamp.modified = file.getLastModificationTime().toMilliseconds();
    if (! file.hasFileExtension("cue")) return stamp;
    const auto data = cueDataFile(file);
    if (! data.existsAsFile()) return stamp;
    juce::uint64 mixed = (juce::uint64) data.getLastModificationTime().toMilliseconds();
    mixed = mixed * 1099511628211ull ^ (juce::uint64) data.getSize();
    mixed ^= (juce::uint64) stamp.modified * 31;
    stamp.size = data.getSize();
    stamp.modified = (int64_t) mixed;
    return stamp;
}
bool isDiscContentFormat(const juce::File& file)
{
    return file.hasFileExtension("iso;img;bin;cue");
}
bool isDiscContentCandidate(const juce::File& file)
{
    if (! isDiscContentFormat(file)) return false;
    // A raw data file with a cue of the same stem is represented by the cue alone.
    if (! file.hasFileExtension("bin;img")) return true;
    // Case-insensitive like the browser's cue lookup; otherwise the same contents appear twice.
    for (const auto& entry : juce::RangedDirectoryIterator(file.getParentDirectory(), false, "*.cue",
                                                          juce::File::findFiles,
                                                          juce::File::FollowSymlinks::no))
        if (entry.getFile().getFileNameWithoutExtension().equalsIgnoreCase(file.getFileNameWithoutExtension()))
            return false;
    return true;
}
DiscIndexSummary indexDiscContents(SampleRepository& repo, const std::vector<juce::File>& images,
                                   CancelCheck cancel,
                                   const std::function<void(int, int, const juce::String&)>& progress)
{
    DiscIndexSummary summary;
    DiscBrowserService service;
    const int total = (int) images.size();
    for (int index = 0; index < total; ++index)
    {
        if (cancel && cancel()) break;
        const auto& image = images[(size_t) index];
        ++summary.imagesTried;
        if (progress) progress(index + 1, total, image.getFileName());
        const auto imagePath = image.getFullPathName();
        const auto stamp = discImageStamp(image);
        const auto size = stamp.size;
        const auto modified = stamp.modified;
        if (const auto state = repo.discIndexState(imagePath);
            state && state->imageSize == size && state->imageModified == modified)
        {
            ++summary.imagesSkipped;
            continue;
        }
        DiscBrowserService::Completion opened;
        service.requestOpen(image);
        if (! waitFor(service, DiscBrowserService::Kind::open, opened, cancel)) break;
        if (opened.status != DiscStatus::ready)
        {
            repo.invalidateDiscState(imagePath);
            ++summary.imagesFailed;
            summary.lastError = image.getFileName() + ": "
                + (opened.error.isNotEmpty() ? opened.error : juce::String("cannot open image"));
            continue;
        }
        if (opened.probe.kind != DiscImageKind::iso9660 && opened.probe.kind != DiscImageKind::akaiRaw)
        {
            repo.invalidateDiscState(imagePath);
            ++summary.imagesFailed;
            summary.lastError = image.getFileName() + ": unsupported disc layout";
            continue;
        }
        DiscBrowserService::Completion listing;
        service.requestList(opened.root, true);
        if (! waitFor(service, DiscBrowserService::Kind::list, listing, cancel)) break;
        if (listing.status != DiscStatus::ready)
        {
            repo.invalidateDiscState(imagePath);
            ++summary.imagesFailed;
            summary.lastError = image.getFileName() + ": "
                + (listing.error.isNotEmpty() ? listing.error : juce::String("cannot list image"));
            continue;
        }
        const bool akai = opened.probe.kind == DiscImageKind::akaiRaw;
        std::map<DiscEntryId, const DiscEntry*> byId;
        for (const auto& entry : listing.listing.entries) byId[entry.id] = &entry;
        std::vector<SampleRecord> rows;
        int visited = 0;
        for (const auto& entry : listing.listing.entries)
        {
            if ((++visited & 255) == 0 && cancel && cancel()) break;
            if (entry.kind != DiscEntryKind::file || entry.capability.isNotEmpty()) continue;
            if (! akai && ! audioName(entry.displayName)) continue;
            const auto path = internalPath(byId, entry, opened.root);
            rows.push_back(makeRow(image, entry, path, akai));
        }
        const bool partial = listing.listing.partial;
        repo.replaceDiscContents(imagePath, size, modified, rows, ! partial);
        if (partial)
        {
            // Keep what was found, but do not treat the image as fully walked.
            repo.invalidateDiscState(imagePath);
            if (summary.lastError.isEmpty())
                summary.lastError = image.getFileName() + ": "
                    + (listing.listing.warning.isNotEmpty() ? listing.listing.warning
                                                            : juce::String("partial listing"));
        }
        ++summary.imagesIndexed;
        summary.entries += (int) rows.size();
    }
    return summary;
}
}
