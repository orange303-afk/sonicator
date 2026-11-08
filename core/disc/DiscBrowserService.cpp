#include "DiscBrowserService.h"
#include "AkaiFilesystem.h"
#include "RawDiscSource.h"
#include "SectorLayout.h"
namespace sonicator {
DiscBrowserService::DiscBrowserService(DiscLimits limits) : budget(limits)
{
    worker = std::thread([this] { run(); });
}
DiscBrowserService::~DiscBrowserService()
{
    {
        std::lock_guard lock(mutex);
        stopping = true;
        hasPending = false;
        ++generation;
    }
    workCondition.notify_all();
    doneCondition.notify_all();
    if (worker.joinable()) worker.join();
}
void DiscBrowserService::requestOpen(const juce::File& file, int trackNumber)
{
    {
        std::lock_guard lock(mutex);
        pending = { Kind::open, file, {}, {}, {}, trackNumber, ++generation };
        hasPending = true;
    }
    workCondition.notify_one();
}
void DiscBrowserService::requestList(DiscEntryId entry)
{
    {
        std::lock_guard lock(mutex);
        pending = { Kind::list, {}, entry, {}, {}, -1, ++generation };
        hasPending = true;
    }
    workCondition.notify_one();
}
void DiscBrowserService::requestDecode(DiscEntryId entry, const juce::String& hint, DiscEntryId partner)
{
    {
        std::lock_guard lock(mutex);
        pending = { Kind::decode, {}, entry, partner, hint, -1, ++generation };
        hasPending = true;
    }
    workCondition.notify_one();
}
void DiscBrowserService::close()
{
    {
        std::lock_guard lock(mutex);
        ++generation;
        hasPending = false;
        closed = true;
        completed.clear();
    }
    doneCondition.notify_all();
}
bool DiscBrowserService::poll(Completion& completion)
{
    std::lock_guard lock(mutex);
    if (completed.empty()) return false;
    completion = std::move(completed.front());
    completed.erase(completed.begin());
    return true;
}
CancelCheck DiscBrowserService::cancelCheck(juce::uint64 forGeneration)
{
    return [this, forGeneration]
    {
        std::lock_guard lock(mutex);
        return stopping || forGeneration != generation;
    };
}
void DiscBrowserService::publish(Completion completion)
{
    std::lock_guard lock(mutex);
    if (completion.generation != generation) return; // superseded while running
    completed.push_back(std::move(completion));
    doneCondition.notify_all();
}
void DiscBrowserService::handle(const Request& request, Completion& completion)
{
    const auto cancel = cancelCheck(request.generation);
    if (request.kind == Kind::open)
    {
        source.reset();
        filesystem.reset();
        const auto raw = openRawDisc(request.file, budget);
        if (! raw.ok()) { completion.status = raw.status; completion.error = raw.error; return; }
        const auto extension = request.file.getFileExtension().toLowerCase();
        juce::File cue = extension == ".cue" ? request.file : juce::File();
        if (cue == juce::File())
        {
            const auto stem = request.file.getFileNameWithoutExtension();
            std::vector<juce::File> matches;
            for (const auto& entry : juce::RangedDirectoryIterator(request.file.getParentDirectory(), false,
                                                                   "*.cue", juce::File::findFiles,
                                                                   juce::File::FollowSymlinks::no))
                if (entry.getFile().getFileNameWithoutExtension().equalsIgnoreCase(stem))
                    matches.push_back(entry.getFile());
            if (matches.size() > 1)
            {
                completion.status = DiscStatus::ambiguous;
                completion.error = "several cue files match this image";
                return;
            }
            if (matches.size() == 1) cue = matches[0];
        }
        if (cue != juce::File())
        {
            handleCue(request, completion, cue);
            return;
        }
        const auto probe = probeDisc(*raw.value, budget, cancel);
        completion.probe = probe.value;
        if (probe.value.kind == DiscImageKind::mdx)
        {
            completion.status = DiscStatus::unsupported;
            completion.error = "MDX requires a separate decoder and licence gate";
            return;
        }
        if (! probe.ok())
        {
            completion.status = probe.status;
            completion.error = probe.error;
            return;
        }
        if (probe.value.kind == DiscImageKind::akaiRaw)
        {
            auto opened = AkaiFilesystem::open(raw.value, budget, cancel);
            if (! opened.ok()) { completion.status = opened.status; completion.error = opened.error; return; }
            filesystem = std::move(opened.value);
        }
        else if (probe.value.kind == DiscImageKind::iso9660)
        {
            TrackLayout layout;
            layout.sectorCount = raw.value->size() / 2048;
            layout.profile = SectorProfile::cooked2048;
            auto track = makeTrackSource(raw.value, layout, budget);
            if (! track.ok()) { completion.status = track.status; completion.error = track.error; return; }
            auto opened = Iso9660Filesystem::open(track.value, budget, cancel);
            if (! opened.ok()) { completion.status = opened.status; completion.error = opened.error; return; }
            filesystem = std::move(opened.value);
        }
        else
        {
            completion.status = DiscStatus::unknown;
            completion.error = "unrecognised disc image layout";
            return;
        }
        if (cancel())
        {
            completion.status = DiscStatus::cancelled;
            return;
        }
        source = raw.value;
        rootId = filesystem->root();
        closed = false;
        completion.root = rootId;
        completion.status = DiscStatus::ready;
        return;
    }
    if (filesystem == nullptr || closed)
    {
        completion.status = DiscStatus::unknown;
        completion.error = "no image is open";
        return;
    }
    if (request.kind == Kind::list)
    {
        auto listed = filesystem->list(request.entry, cancel);
        completion.listing = std::move(listed.value);
        completion.status = listed.status;
        completion.error = listed.error;
        return;
    }
    if (auto* akai = dynamic_cast<AkaiFilesystem*>(filesystem.get()))
    {
        auto decoded = akai->decodeSample(request.entry, cancel);
        completion.audio = std::move(decoded.value);
        completion.status = decoded.status;
        completion.error = decoded.error;
        return;
    }
    auto decodeOne = [&](DiscEntryId id)
    {
        const auto extent = filesystem->openFile(id, cancel);
        if (! extent.ok()) return DiscResult<SampleAudioPtr> { extent.status, {}, extent.error };
        return decodeDiscAudio(extent.value, request.hint, budget, cancel);
    };
    auto left = decodeOne(request.entry);
    if (! left.ok() || request.partner.value == 0)
    {
        completion.audio = std::move(left.value);
        completion.status = left.status;
        completion.error = left.error;
        return;
    }
    auto right = decodeOne(request.partner);
    if (! right.ok()) { completion.status = right.status; completion.error = right.error; return; }
    if (left.value->channels != 1 || right.value->channels != 1
        || left.value->frames != right.value->frames
        || left.value->sampleRate != right.value->sampleRate)
    {
        completion.status = DiscStatus::malformed;
        completion.error = "stereo pair differs in channels, length or sample rate";
        return;
    }
    auto stereo = std::make_shared<SampleAudio>();
    stereo->sampleRate = left.value->sampleRate;
    stereo->channels = 2;
    stereo->bitsPerSample = left.value->bitsPerSample;
    stereo->floatingPoint = left.value->floatingPoint;
    stereo->frames = left.value->frames;
    stereo->left = std::move(left.value->left);
    stereo->right = std::move(right.value->left);
    completion.audio = std::move(stereo);
    completion.status = DiscStatus::ready;
}
void DiscBrowserService::handleCue(const Request& request, Completion& completion, const juce::File& cue)
{
    const auto cancel = cancelCheck(request.generation);
    auto sheetResult = openCueSheet(cue, budget, cancel);
    if (! sheetResult.ok())
    {
        completion.status = sheetResult.status;
        completion.error = sheetResult.error;
        return;
    }
    auto& sheet = sheetResult.value;
    const auto dataTracks = sheet.dataTrackIndices();
    if (dataTracks.empty())
    {
        completion.status = DiscStatus::unsupported;
        completion.error = "the cue sheet has no data track";
        return;
    }
    int chosen = -1;
    if (request.trackNumber >= 0)
    {
        for (int index : dataTracks)
            if (sheet.tracks[(size_t) index].number == request.trackNumber) { chosen = index; break; }
        if (chosen < 0)
        {
            completion.status = DiscStatus::unknown;
            completion.error = "the requested data track is not in this cue sheet";
            return;
        }
    }
    else if (dataTracks.size() == 1) chosen = dataTracks[0];
    else
    {
        completion.status = DiscStatus::ambiguous;
        completion.error = "multiple data tracks; choose one";
        for (int index : dataTracks) completion.cueTracks.push_back(sheet.tracks[(size_t) index]);
        return;
    }
    const auto layout = cueTrackLayout(sheet, chosen);
    if (! layout.ok())
    {
        completion.status = layout.status;
        completion.error = layout.error;
        return;
    }
    const auto raw = openRawDisc(layout.value.file, budget);
    if (! raw.ok()) { completion.status = raw.status; completion.error = raw.error; return; }
    TrackLayout track;
    track.fileOffset = layout.value.fileOffset;
    track.sectorCount = layout.value.sectorCount;
    track.profile = layout.value.profile;
    auto blocks = makeTrackSource(raw.value, track, budget);
    if (! blocks.ok()) { completion.status = blocks.status; completion.error = blocks.error; return; }
    auto bytes = std::make_shared<BlockByteSource>(blocks.value);
    const auto probe = probeDisc(*bytes, budget, cancel);
    completion.probe = probe.value;
    if (! probe.ok())
    {
        completion.status = probe.status == DiscStatus::ready ? DiscStatus::unsupported : probe.status;
        completion.error = probe.error.isNotEmpty() ? probe.error : "the data track has no supported filesystem";
        return;
    }
    if (probe.value.kind == DiscImageKind::iso9660)
    {
        auto opened = Iso9660Filesystem::open(blocks.value, budget, cancel);
        if (! opened.ok()) { completion.status = opened.status; completion.error = opened.error; return; }
        filesystem = std::move(opened.value);
    }
    else if (probe.value.kind == DiscImageKind::akaiRaw)
    {
        auto opened = AkaiFilesystem::open(bytes, budget, cancel);
        if (! opened.ok()) { completion.status = opened.status; completion.error = opened.error; return; }
        filesystem = std::move(opened.value);
    }
    else
    {
        completion.status = DiscStatus::unsupported;
        completion.error = "the data track is not ISO-9660 or raw Akai";
        return;
    }
    if (cancel()) { completion.status = DiscStatus::cancelled; return; }
    source = bytes;
    rootId = filesystem->root();
    completion.root = rootId;
    closed = false;
    completion.status = DiscStatus::ready;
}

void DiscBrowserService::run()
{
    for (;;)
    {
        Request request;
        {
            std::unique_lock lock(mutex);
            workCondition.wait(lock, [this] { return stopping || hasPending; });
            if (stopping) return;
            request = pending;
            hasPending = false;
        }
        Completion completion;
        completion.kind = request.kind;
        completion.generation = request.generation;
        completion.file = request.file;
        completion.entry = request.entry;
        try
        {
            handle(request, completion);
        }
        catch (const std::exception& e)
        {
            completion.status = DiscStatus::malformed;
            completion.error = juce::String::fromUTF8(e.what());
        }
        catch (...)
        {
            completion.status = DiscStatus::malformed;
            completion.error = "disc operation failed";
        }
        publish(std::move(completion));
    }
}
} // namespace sonicator
