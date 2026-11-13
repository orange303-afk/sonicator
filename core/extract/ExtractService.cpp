#include "ExtractService.h"
#include "EmbeddedSamples.h"
#include "core/disc/DiscBrowserService.h"
#include "core/export/AudioExporter.h"
#include <algorithm>
#include <cmath>
namespace sonicator {
namespace {
bool isForbiddenFilenameChar(juce::juce_wchar c)
{
    return c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\'
        || c == '|' || c == '?' || c == '*' || c < 32;
}
bool isAvailable(const juce::File& file)
{
    return ! file.exists() && ! juce::File(file.getFullPathName() + ".part").exists();
}
juce::String folderStem(const juce::String& name)
{
    juce::String stem;
    for (int i = 0; i < name.length(); ++i)
    {
        const auto c = name[i];
        stem += juce::String::charToString(isForbiddenFilenameChar(c) ? '_' : c);
    }
    stem = stem.trim();
    while (stem.contains("  ")) stem = stem.replace("  ", " ");
    stem = stem.trimCharactersAtEnd(" .");
    if (stem.isEmpty()) stem = "Instrument";
    return stem.substring(0, 80);
}
juce::File uniqueSampleFolder(const juce::File& parent, const juce::String& stem)
{
    auto candidate = parent.getChildFile(stem + " samples");
    if (isAvailable(candidate)) return candidate;
    for (int attempt = 2; attempt <= 999; ++attempt)
    {
        candidate = parent.getChildFile(stem + " samples " + juce::String(attempt));
        if (isAvailable(candidate)) return candidate;
    }
    return {};
}
juce::File uniqueSampleFile(const juce::File& folder, int ordinal, const juce::String& name)
{
    const auto base = extractSampleFileName(ordinal, name);
    auto candidate = folder.getChildFile(base);
    if (isAvailable(candidate)) return candidate;
    const auto stem = base.upToLastOccurrenceOf(".wav", false, false);
    for (int attempt = 2; attempt <= 99; ++attempt)
    {
        candidate = folder.getChildFile(stem + " (" + juce::String(attempt) + ").wav");
        if (isAvailable(candidate)) return candidate;
    }
    return {};
}
}
juce::String extractSampleFileName(int ordinal, const juce::String& name)
{
    juce::String stem;
    for (int i = 0; i < name.length(); ++i)
    {
        const auto c = name[i];
        stem += juce::String::charToString(isForbiddenFilenameChar(c) ? '_' : c);
    }
    stem = stem.trim();
    while (stem.contains("  ")) stem = stem.replace("  ", " ");
    stem = stem.trimCharactersAtEnd(" .");
    // XM/MOD sample names are often file names; do not turn "Uhhuhu.wav" into "Uhhuhu.wav.wav".
    if (stem.endsWithIgnoreCase(".wav"))
        stem = stem.dropLastCharacters(4).trimCharactersAtEnd(" .");
    if (stem.isEmpty()) stem = "Sample";
    stem = stem.substring(0, 80);
    return juce::String(ordinal).paddedLeft('0', 3) + " " + stem + ".wav";
}
ExtractService::~ExtractService()
{
    cancel();
    waitForThreadToExit(-1);
}
bool ExtractService::start(const std::vector<juce::File>& sources, const ExtractOptions& requested)
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    {
        std::lock_guard lock(stateMutex);
        discJobs.clear();
        files = sources;
        options = requested;
        state = {};
        state.running = true;
        state.totalFiles = (int) files.size();
        state.current = "Preparing";
        fileResults.clear();
    }
    cancelled = false;
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.current = "Cannot start extract thread";
        return false;
    }
    return true;
}
bool ExtractService::startDisc(const std::vector<DiscExtractJob>& jobs, const ExtractOptions& requested)
{
    if (isThreadRunning() || jobs.empty()) return false;
    waitForThreadToExit(-1);
    {
        std::lock_guard lock(stateMutex);
        files.clear();
        discJobs = jobs;
        options = requested;
        state = {};
        state.running = true;
        state.totalFiles = (int) discJobs.size();
        state.current = "Preparing";
        fileResults.clear();
    }
    cancelled = false;
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.current = "Cannot start extract thread";
        return false;
    }
    return true;
}
void ExtractService::cancel()
{
    cancelled = true;
    std::lock_guard lock(stateMutex);
    state.cancelled = true;
}
ExtractProgress ExtractService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}
std::vector<ExtractFileResult> ExtractService::results() const
{
    std::lock_guard lock(stateMutex);
    return fileResults;
}
void ExtractService::updateProgress(const std::function<void(ExtractProgress&)>& change)
{
    std::lock_guard lock(stateMutex);
    change(state);
}
void ExtractService::run()
{
    if (! discJobs.empty()) { runDisc(); return; }
    runEmbedded();
}
void ExtractService::runEmbedded()
{
    std::vector<ExtractFileResult> results;
    AudioExporter exporter;
    const auto publishFraction = [this](int completedFiles, int entry, int entryCount)
    {
        updateProgress([completedFiles, entry, entryCount](ExtractProgress& p)
        {
            const double total = p.totalFiles > 0 ? (double) p.totalFiles : 1.0;
            const double within = entryCount > 0 ? (double) entry / (double) entryCount : 0.0;
            p.fraction = juce::jlimit(0.0, 1.0, ((double) completedFiles + within) / total);
        });
    };
    for (size_t index = 0; index < files.size(); ++index)
    {
        if (cancelled.load())
        {
            // Unstarted sources are reported as cancelled, so the dialog's partial summary names
            // everything the batch did not reach.
            for (size_t rest = index; rest < files.size(); ++rest)
            {
                ExtractFileResult skipped;
                skipped.source = files[rest];
                skipped.state = ExtractFileState::cancelled;
                skipped.error = "Cancelled";
                results.push_back(std::move(skipped));
            }
            break;
        }
        const auto source = files[index];
        updateProgress([&source](ExtractProgress& p)
        {
            p.current = source.getFileName();
        });
        ExtractFileResult result;
        result.source = source;
        const auto parent = options.sameFolder ? source.getParentDirectory() : options.outputFolder;
        if (! parent.isDirectory() && ! parent.createDirectory())
        {
            result.state = ExtractFileState::failed;
            result.error = "Cannot create output folder: " + parent.getFullPathName();
            results.push_back(result);
            updateProgress([index, &results](ExtractProgress& p)
            {
                p.processedFiles = (int) index + 1;
                p.failed += 1;
                p.current = results.back().source.getFileName();
            });
            publishFraction((int) index + 1, 0, 0);
            continue;
        }
        EmbeddedSampleSet set;
        juce::String error;
        if (! set.open(source, error))
        {
            result.state = ExtractFileState::failed;
            result.error = error.isNotEmpty() ? error : "Cannot read embedded samples";
            results.push_back(result);
            updateProgress([index, &results](ExtractProgress& p)
            {
                p.processedFiles = (int) index + 1;
                p.failed += 1;
                p.current = results.back().source.getFileName();
            });
            publishFraction((int) index + 1, 0, 0);
            continue;
        }
        result.empty = set.emptyCount();
        const int entryCount = (int) set.entries().size();
        updateProgress([&result, entryCount](ExtractProgress& p)
        {
            p.totalSamples += entryCount;
            p.skippedEmpty += result.empty;
        });
        const auto folder = uniqueSampleFolder(parent,
            folderStem(source.getFileNameWithoutExtension()));
        if (folder == juce::File() || ! folder.createDirectory())
        {
            result.state = ExtractFileState::failed;
            result.error = "Cannot create sample folder beside " + source.getFileName();
            results.push_back(result);
            updateProgress([index, &results](ExtractProgress& p)
            {
                p.processedFiles = (int) index + 1;
                p.failed += 1;
            });
            publishFraction((int) index + 1, 0, 0);
            continue;
        }
        result.folder = folder;
        int ordinal = 0;
        for (const auto& entry : set.entries())
        {
            ++ordinal;
            if (cancelled.load())
            {
                result.state = ExtractFileState::cancelled;
                result.error = "Cancelled";
                break;
            }
            const auto audio = set.read(entry.index, error);
            if (audio == nullptr)
            {
                ++result.failed;
                result.problems.add(entry.name + ": "
                    + (error.isNotEmpty() ? error : juce::String("Cannot decode the sample")));
                updateProgress([&result](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.failed;
                });
                publishFraction((int) index, ordinal, entryCount);
                continue;
            }
            if (AudioExporter::isEntirelySilent(*audio))
            {
                ++result.silent;
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.skippedSilent;
                });
                publishFraction((int) index, ordinal, entryCount);
                continue;
            }
            auto audible = audio;
            if (options.tuneToSong)
            {
                const double ratio = set.playbackRatio(entry.index);
                if (std::abs(ratio - 1.0) > 1e-9)
                {
                    const auto tuned = resampleForPlayback(*audio, ratio);
                    if (tuned != nullptr) audible = tuned;
                }
            }
            const auto destination = uniqueSampleFile(folder, ordinal, entry.name);
            if (destination == juce::File())
            {
                ++result.failed;
                result.problems.add(entry.name + ": Cannot allocate a unique file name");
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.failed;
                });
                publishFraction((int) index, ordinal, entryCount);
                continue;
            }
            const auto chunk = set.samplerChunk(entry.index);
            const auto exported = exporter.exportSampleFile(*audible, destination, options.normalize,
                [this] { return cancelled.load(); }, chunk.has_value() ? &*chunk : nullptr);
            if (! exported.success)
            {
                if (exported.error.containsIgnoreCase("cancel"))
                {
                    result.state = ExtractFileState::cancelled;
                    result.error = "Cancelled";
                    updateProgress([](ExtractProgress& p) { p.cancelled = true; });
                    break;
                }
                ++result.failed;
                result.problems.add(entry.name + ": " + exported.error);
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.failed;
                });
            }
            else
            {
                ++result.written;
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.written;
                });
            }
            publishFraction((int) index, ordinal, entryCount);
        }
        if (result.state != ExtractFileState::cancelled) result.state = ExtractFileState::extracted;
        results.push_back(result);
        updateProgress([index](ExtractProgress& p)
        {
            p.processedFiles = (int) index + 1;
            p.current.clear();
        });
        publishFraction((int) index + 1, 0, 0);
        if (result.state == ExtractFileState::cancelled)
        {
            for (size_t rest = index + 1; rest < files.size(); ++rest)
            {
                ExtractFileResult skipped;
                skipped.source = files[rest];
                skipped.state = ExtractFileState::cancelled;
                skipped.error = "Cancelled";
                results.push_back(std::move(skipped));
            }
            break;
        }
    }
    // Results and the terminal progress state are published together: the owner polls progress(),
    // and seeing running == false must mean results() is already complete.
    std::lock_guard lock(stateMutex);
    fileResults = std::move(results);
    state.running = false;
    state.processedFiles = (int) files.size();
    state.fraction = state.cancelled ? state.fraction : 1.0;
    state.current.clear();
}

namespace {
DiscBrowserService::Completion waitForDisc(DiscBrowserService& service, DiscBrowserService::Kind kind,
                                           std::atomic<bool>& cancelled, juce::String& error)
{
    for (int waited = 0; waited < 120000; waited += 10)
    {
        if (cancelled.load())
        {
            error = "Cancelled";
            service.close();
            return {};
        }
        DiscBrowserService::Completion next;
        while (service.poll(next))
            if (next.kind == kind) return next;
        juce::Thread::sleep(10);
    }
    error = "The image did not respond";
    service.close();
    return {};
}
} // namespace

void ExtractService::runDisc()
{
    std::vector<ExtractFileResult> results;
    AudioExporter exporter;
    const int jobCount = (int) discJobs.size();
    const auto publishFraction = [this, jobCount](int completedJobs, int entry, int entryCount)
    {
        updateProgress([completedJobs, entry, entryCount, jobCount](ExtractProgress& p)
        {
            const double total = jobCount > 0 ? (double) jobCount : 1.0;
            const double within = entryCount > 0 ? (double) entry / (double) entryCount : 0.0;
            p.fraction = juce::jlimit(0.0, 1.0, ((double) completedJobs + within) / total);
        });
    };
    const auto cancelJob = [this, &results, jobCount](ExtractFileResult& result, int index)
    {
        result.state = ExtractFileState::cancelled;
        result.error = "Cancelled";
        updateProgress([](ExtractProgress& p) { p.cancelled = true; });
        results.push_back(result);
        for (int rest = index + 1; rest < jobCount; ++rest)
        {
            ExtractFileResult skipped;
            skipped.source = discJobs[(size_t) rest].image;
            skipped.state = ExtractFileState::cancelled;
            skipped.error = "Cancelled";
            results.push_back(std::move(skipped));
        }
    };
    const auto failJob = [this, &results, publishFraction](ExtractFileResult& result, int index,
                                                           const juce::String& error)
    {
        result.state = ExtractFileState::failed;
        result.error = error.isNotEmpty() ? error : juce::String("Cannot read the image");
        results.push_back(result);
        updateProgress([index](ExtractProgress& p)
        {
            p.processedFiles = index + 1;
            p.failed += 1;
            p.current.clear();
        });
        publishFraction(index + 1, 0, 0);
    };
    for (int index = 0; index < jobCount; ++index)
    {
        if (cancelled.load())
        {
            for (int rest = index; rest < jobCount; ++rest)
            {
                ExtractFileResult skipped;
                skipped.source = discJobs[(size_t) rest].image;
                skipped.state = ExtractFileState::cancelled;
                skipped.error = "Cancelled";
                results.push_back(std::move(skipped));
            }
            break;
        }
        const auto job = discJobs[(size_t) index];
        ExtractFileResult result;
        result.source = job.image;
        updateProgress([&job](ExtractProgress& p) { p.current = job.image.getFileName(); });

        const auto parent = options.sameFolder ? job.image.getParentDirectory() : options.outputFolder;
        if (! parent.isDirectory() && ! parent.createDirectory())
        {
            failJob(result, index, "Cannot create output folder: " + parent.getFullPathName());
            continue;
        }
        DiscBrowserService service;
        juce::String error;
        service.requestOpen(job.image);
        const auto opened = waitForDisc(service, DiscBrowserService::Kind::open, cancelled, error);
        if (cancelled.load()) { cancelJob(result, index); break; }
        if (opened.status != DiscStatus::ready)
        {
            failJob(result, index, opened.error.isNotEmpty() ? opened.error : error);
            continue;
        }
        // The filesystem records sample idents while it lists, so a selected entry is decoded
        // only after the same recursive listing the browser performs.
        service.requestList(opened.root, true);
        const auto listed = waitForDisc(service, DiscBrowserService::Kind::list, cancelled, error);
        if (cancelled.load()) { cancelJob(result, index); break; }
        if (listed.status != DiscStatus::ready)
        {
            failJob(result, index, listed.error.isNotEmpty() ? listed.error : error);
            continue;
        }
        std::vector<DiscExtractEntry> entries = job.entries;
        if (entries.empty())
            for (const auto& entry : listed.listing.entries)
                if (entry.kind == DiscEntryKind::file && entry.capability.isEmpty())
                    entries.push_back({ entry.id, entry.partner, entry.displayName });
        const auto folder = uniqueSampleFolder(parent,
            folderStem(job.image.getFileNameWithoutExtension()));
        if (folder == juce::File() || ! folder.createDirectory())
        {
            failJob(result, index, "Cannot create sample folder beside " + job.image.getFileName());
            continue;
        }
        result.folder = folder;
        const int entryCount = (int) entries.size();
        updateProgress([entryCount](ExtractProgress& p) { p.totalSamples += entryCount; });
        int ordinal = 0;
        for (const auto& entry : entries)
        {
            ++ordinal;
            if (cancelled.load())
            {
                result.state = ExtractFileState::cancelled;
                result.error = "Cancelled";
                updateProgress([](ExtractProgress& p) { p.cancelled = true; });
                break;
            }
            service.requestDecode(entry.id, entry.name, entry.partner);
            const auto decoded = waitForDisc(service, DiscBrowserService::Kind::decode, cancelled, error);
            if (cancelled.load())
            {
                result.state = ExtractFileState::cancelled;
                result.error = "Cancelled";
                updateProgress([](ExtractProgress& p) { p.cancelled = true; });
                break;
            }
            if (decoded.status == DiscStatus::unsupported)
            {
                // A data image holds files that are not audio; skipping them is not a failure.
                ++result.skipped;
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.skippedUnsupported;
                });
                publishFraction(index, ordinal, entryCount);
                continue;
            }
            if (decoded.status != DiscStatus::ready || decoded.audio == nullptr)
            {
                ++result.failed;
                result.problems.add(entry.name + ": "
                    + (decoded.error.isNotEmpty() ? decoded.error
                       : (error.isNotEmpty() ? error : juce::String("Cannot decode the sample"))));
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.failed;
                });
                publishFraction(index, ordinal, entryCount);
                continue;
            }
            if (AudioExporter::isEntirelySilent(*decoded.audio))
            {
                ++result.silent;
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.skippedSilent;
                });
                publishFraction(index, ordinal, entryCount);
                continue;
            }
            const auto destination = uniqueSampleFile(folder, ordinal, entry.name);
            if (destination == juce::File())
            {
                ++result.failed;
                result.problems.add(entry.name + ": Cannot allocate a unique file name");
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.failed;
                });
                publishFraction(index, ordinal, entryCount);
                continue;
            }
            const auto exported = exporter.exportSampleFile(*decoded.audio, destination,
                options.normalize, [this] { return cancelled.load(); });
            if (! exported.success)
            {
                if (exported.error.containsIgnoreCase("cancel"))
                {
                    result.state = ExtractFileState::cancelled;
                    result.error = "Cancelled";
                    updateProgress([](ExtractProgress& p) { p.cancelled = true; });
                    break;
                }
                ++result.failed;
                result.problems.add(entry.name + ": " + exported.error);
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.failed;
                });
            }
            else
            {
                ++result.written;
                updateProgress([](ExtractProgress& p)
                {
                    ++p.processedSamples;
                    ++p.written;
                });
            }
            publishFraction(index, ordinal, entryCount);
        }
        if (result.state != ExtractFileState::cancelled) result.state = ExtractFileState::extracted;
        results.push_back(result);
        updateProgress([index](ExtractProgress& p)
        {
            p.processedFiles = index + 1;
            p.current.clear();
        });
        publishFraction(index + 1, 0, 0);
        if (result.state == ExtractFileState::cancelled)
        {
            for (int rest = index + 1; rest < jobCount; ++rest)
            {
                ExtractFileResult skipped;
                skipped.source = discJobs[(size_t) rest].image;
                skipped.state = ExtractFileState::cancelled;
                skipped.error = "Cancelled";
                results.push_back(std::move(skipped));
            }
            break;
        }
    }
    std::lock_guard lock(stateMutex);
    fileResults = std::move(results);
    state.running = false;
    state.processedFiles = jobCount;
    state.fraction = state.cancelled ? state.fraction : 1.0;
    state.current.clear();
}

bool isDiscExtractSource(const juce::File& file)
{
    return file.existsAsFile() && file.hasFileExtension("iso;img;bin;cue");
}
} // namespace sonicator
