#include "ExternalFileDrag.h"
#include "AudioExporter.h"
#include "core/emu/EblReader.h"
#include <algorithm>
#include <vector>
namespace sonicator {
namespace {
juce::String pathHash(const juce::String& path)
{
    return juce::SHA256(path.toRawUTF8(), (size_t) path.getNumBytesAsUTF8()).toHexString().substring(0, 12);
}
bool writeEblWav(const EblSample& sample, const juce::File& destination)
{
    destination.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();
    if (stream == nullptr) return false;
    auto options = juce::AudioFormatWriterOptions {}
        .withSampleRate((double) sample.sampleRate)
        .withNumChannels(sample.channels)
        .withBitsPerSample(16);
    if (sample.loop)
        options = options.withMetadata("NumSampleLoops", "1")
                         .withMetadata("Loop0Start", juce::String(sample.loop->start))
                         // The decoder's end is exclusive; RIFF smpl stores the last frame played.
                         .withMetadata("Loop0End", juce::String(sample.loop->end - 1))
                         .withMetadata("Loop0Type", "0");
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, options);
    if (writer == nullptr) return false;
    // JUCE's fixed-point convention is int16 << 16, so the PCM round-trips byte for byte.
    constexpr int blockFrames = 65536;
    std::vector<std::vector<int>> channels((size_t) sample.channels,
                                           std::vector<int>((size_t) blockFrames));
    std::vector<const int*> pointers;
    for (auto& channel : channels) pointers.push_back(channel.data());
    for (int64_t offset = 0; offset < sample.frames; offset += blockFrames)
    {
        const int count = (int) juce::jmin<int64_t>(blockFrames, sample.frames - offset);
        for (int channel = 0; channel < sample.channels; ++channel)
            for (int i = 0; i < count; ++i)
                channels[(size_t) channel][(size_t) i] =
                    (int) sample.pcm[(size_t) ((offset + i) * sample.channels + channel)] << 16;
        if (! writer->write(pointers.data(), count)) return false;
    }
    writer.reset();
    return destination.existsAsFile() && destination.getSize() > 0;
}
} // namespace

juce::StringArray ExternalFileDrag::existingPaths(const juce::StringArray& candidates)
{
    juce::StringArray files;
    for (const auto& path : candidates)
        if (juce::File(path).existsAsFile()) files.add(path);
    return files;
}
juce::StringArray ExternalFileDrag::selectedPaths(const std::vector<SampleRecord>& visible,
                                                  const std::vector<int>& selectedRows)
{
    auto rows = selectedRows;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    juce::StringArray files;
    for (const int row : rows)
        if (row >= 0 && row < (int) visible.size())
            if (juce::File(visible[(size_t) row].filePath).existsAsFile())
                files.add(visible[(size_t) row].filePath);
    return files;
}
bool ExternalFileDrag::exceedsThreshold(int dx, int dy) { return dx * dx + dy * dy > 25; }
juce::File ExternalFileDrag::dragCacheFolder()
{
    return AudioExporter::defaultExportFolder().getChildFile("drag");
}
juce::StringArray ExternalFileDrag::materialiseForDrag(const juce::StringArray& paths,
                                                       const juce::File& cacheDir,
                                                       juce::StringArray& problems)
{
    juce::StringArray files;
    for (const auto& path : paths)
    {
        const juce::File source(path);
        if (! isEblFile(source))
        {
            files.add(path);
            continue;
        }
        const auto parsed = readEblFile(source);
        if (! parsed.ok())
        {
            problems.add(source.getFileName() + ": " + parsed.error);
            continue;
        }
        auto stem = AudioExporter::sanitizeStem(parsed.sample->name.isNotEmpty()
                                                    ? parsed.sample->name
                                                    : source.getFileNameWithoutExtension());
        if (stem.isEmpty()) stem = "sample";
        cacheDir.createDirectory();
        const auto destination = cacheDir.getChildFile(stem + "-" + pathHash(path) + ".wav");
        const bool reusable = destination.existsAsFile()
            && destination.getLastModificationTime() >= source.getLastModificationTime();
        if (! reusable && ! writeEblWav(*parsed.sample, destination))
        {
            destination.deleteFile();
            problems.add(source.getFileName() + ": cannot write " + destination.getFileName());
            continue;
        }
        files.add(destination.getFullPathName());
    }
    return files;
}
bool ExternalFileDrag::start(const juce::StringArray& candidates, juce::Component* source,
                             DragFunction drag)
{
    auto files = existingPaths(candidates);
    if (files.isEmpty()) return false;
    bool hasEbl = false;
    for (const auto& path : files)
        if (isEblFile(juce::File(path))) { hasEbl = true; break; }
    if (hasEbl)
    {
        juce::StringArray problems;
        files = materialiseForDrag(files, dragCacheFolder(), problems);
        if (files.isEmpty()) return false;
    }
    if (drag) return drag(files, false, source);
    return juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, source);
}
} // namespace sonicator
