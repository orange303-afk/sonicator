#include "SplitService.h"
#include "core/audio/SonicatorFormats.h"
#include "core/export/AudioExporter.h"
#include "core/scanner/FileScanner.h"
#include <algorithm>
#include <cmath>
namespace sonicator {
namespace {
juce::String splitStem(const juce::String& name)
{
    juce::String out;
    for (auto c : name)
    {
        const bool forbidden = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\'
            || c == '|' || c == '?' || c == '*' || c < 32;
        out += juce::String::charToString(forbidden ? '_' : c);
    }
    out = out.trimCharactersAtEnd(" .");
    if (out.isEmpty()) out = "audio";
    return out.substring(0, 120);
}
juce::File uniquePartFile(const juce::File& folder, const juce::String& stem, int index, int width)
{    const auto padded = juce::String(index).paddedLeft('0', width);
    const auto available = [](const juce::File& file)
    {
        return ! file.existsAsFile() && ! juce::File(file.getFullPathName() + ".part").existsAsFile();
    };
    auto candidate = folder.getChildFile(stem + "_" + padded + ".wav");
    if (available(candidate)) return candidate;
    for (int attempt = 1; attempt <= 99; ++attempt)
    {
        candidate = folder.getChildFile(stem + "_" + padded + "-" + juce::String(attempt) + ".wav");
        if (available(candidate)) return candidate;
    }
    return {};
}
}
juce::StringArray splitSidecarPaths(const juce::File& source)
{
    juce::StringArray out;
    const auto parent = source.getParentDirectory();
    const auto stem = source.getFileNameWithoutExtension();
    for (const auto& name : { source.getFileName() + ".asd", stem + ".asd", stem + ".pk" })
    {
        const auto file = parent.getChildFile(name);
        if (! file.existsAsFile()) continue;
        if (! out.contains(file.getFullPathName())) out.add(file.getFullPathName());
    }
    return out;
}
SplitService::~SplitService()
{
    cancel();
    waitForThreadToExit(-1);
}bool SplitService::start(const std::vector<juce::File>& sources, const SplitOptions& options)
{
    if (isThreadRunning()) return false;
    waitForThreadToExit(-1);
    {
        std::lock_guard lock(stateMutex);
        files = sources;
        splitOptions = options;
        state = {};
        state.running = true;
        state.total = (int) files.size();
        state.message = "Splitting";
        fileResults.clear();
    }
    cancelled = false;
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.message = "Cannot start split thread";
        return false;
    }
    return true;
}
void SplitService::cancel()
{
    cancelled = true;
}
SplitProgress SplitService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}
std::vector<SplitFileResult> SplitService::results() const
{
    std::lock_guard lock(stateMutex);
    return fileResults;
}
void SplitService::run()
{
    const auto cancelCheck = [this] { return cancelled.load(); };
    for (const auto& source : files)
    {
        SplitFileResult result;
        result.source = source;
        if (cancelled)
        {
            result.state = SplitFileState::cancelled;
            std::lock_guard lock(stateMutex);
            fileResults.push_back(result);
            ++state.processed;
            break;
        }
        {
            std::lock_guard lock(stateMutex);
            state.current = source.getFileName();
        }
        const auto finish = [&](SplitFileState fileState, juce::String error = {})
        {
            result.state = fileState;
            result.error = std::move(error);
            std::lock_guard lock(stateMutex);
            ++state.processed;
            if (fileState == SplitFileState::split) ++state.succeeded;
            else if (fileState == SplitFileState::failed) ++state.failed;
            else if (fileState == SplitFileState::skipped || fileState == SplitFileState::noSplit) ++state.skipped;
            state.current = source.getFileName();
            fileResults.push_back(result);
        };
        if (! source.existsAsFile())
        {
            finish(SplitFileState::skipped, "File is missing");
            continue;
        }
        if (! FileScanner::isSupportedFormat(source) || FileScanner::isMetadataOnlyFormat(source))
        {
            finish(SplitFileState::skipped, "Format cannot be split");
            continue;
        }
        const auto outputFolder = splitOptions.sameFolder ? source.getParentDirectory()
                                                         : splitOptions.outputFolder;
        if (! outputFolder.createDirectory())
        {
            finish(SplitFileState::failed, "Cannot create output folder");
            continue;
        }
        juce::AudioFormatManager formats;
        registerSonicatorFormats(formats);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
        if (reader == nullptr)
        {
            finish(SplitFileState::skipped, "Cannot decode source");
            continue;
        }
        const auto detection = detectSplitParts(*reader, splitOptions.silence, cancelCheck);
        result.detectedParts = (int) detection.parts.size();
        result.discardedShortParts = detection.discardedShortParts;
        if (cancelled)
        {
            finish(SplitFileState::cancelled, "Cancelled");
            break;
        }
        if (detection.parts.size() < 2)
        {
            finish(SplitFileState::noSplit, "No silence found");
            continue;
        }
        const auto stem = splitStem(source.getFileNameWithoutExtension());
        const int width = juce::jmax(2, juce::String((int) detection.parts.size()).length());
        AudioExporter exporter;
        bool failed = false;
        for (int i = 0; i < (int) detection.parts.size(); ++i)
        {
            if (cancelled)
            {
                result.error = "Cancelled";
                failed = true;
                break;
            }
            const auto destination = uniquePartFile(outputFolder, stem, i + 1, width);
            if (destination == juce::File())
            {
                result.error = "Cannot allocate a unique part name";
                failed = true;
                break;
            }
            const auto part = detection.parts[(size_t) i];
            const auto exported = exporter.exportRange(source, part.startSeconds, part.endSeconds,
                                                       destination, cancelCheck, 0.005);
            if (! exported.success)
            {
                result.error = exported.error;
                failed = true;
                break;
            }
            result.parts.push_back(exported.file);
        }
        if (failed)
        {
            finish(cancelled ? SplitFileState::cancelled : SplitFileState::failed, result.error);
            if (cancelled) break;
            continue;
        }
        finish(SplitFileState::split);
    }
    std::lock_guard lock(stateMutex);
    state.running = false;
    state.cancelled = cancelled;
    state.message = cancelled ? "Split cancelled" : "Split complete";
}
} // namespace sonicator
