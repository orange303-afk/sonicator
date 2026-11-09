#include "ExtractService.h"
#include "EmbeddedSamples.h"
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
            const auto exported = exporter.exportSampleFile(*audible, destination, options.normalize,
                [this] { return cancelled.load(); });
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
} // namespace sonicator
