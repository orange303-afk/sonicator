#include "DragMetadata.h"
#include "RiffChunks.h"
#include "core/audio/SonicatorFormats.h"
#include "AudioExporter.h"
#include "core/smart/KeyNames.h"
#include "core/smart/WavMetadata.h"
#include <cmath>

namespace sonicator
{
namespace
{
constexpr int chunkFrames = 65536;

juce::String extensionOf(const juce::File& file)
{
    return file.getFileExtension().toLowerCase();
}

juce::String hashOf(const juce::File& source, const DragMetadata& metadata)
{
    juce::String text = source.getFullPathName() + "|" + juce::String(source.getLastModificationTime().toMilliseconds())
        + "|" + juce::String(source.getSize()) + "|" + (metadata.hasBpm ? juce::String(metadata.bpm, 6) : "none")
        + "|" + juce::String(metadata.hasRoot ? metadata.rootMidi : -1);
    return juce::SHA256(text.toRawUTF8(), (size_t) text.getNumBytesAsUTF8()).toHexString().toLowerCase();
}

bool encodeToWav(const juce::File& source, const juce::File& destination, juce::String& error)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio";
        return false;
    }
    if (reader->numChannels > 2 || reader->lengthInSamples <= 0)
    {
        error = "Only mono and stereo sources can be dragged with metadata";
        return false;
    }
    const bool floating = reader->usesFloatingPointData;
    const int bits = floating ? 32 : 24;
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(reader->sampleRate)
        .withNumChannels((int) reader->numChannels).withBitsPerSample(bits)
        .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                   : juce::AudioFormatWriterOptions::SampleFormat::integral);
    std::unique_ptr<juce::OutputStream> stream(destination.createOutputStream());
    if (stream == nullptr)
    {
        error = "Cannot create the drag copy";
        return false;
    }
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        error = "The WAV writer rejected the source settings";
        return false;
    }
    juce::AudioBuffer<float> buffer((int) reader->numChannels, chunkFrames);
    int64_t frame = 0;
    while (frame < reader->lengthInSamples)
    {
        const auto count = (int) juce::jmin<int64_t>(chunkFrames, reader->lengthInSamples - frame);
        if (! reader->read(&buffer, 0, count, frame, true, true) || ! writer->writeFromAudioSampleBuffer(buffer, 0, count))
        {
            writer.reset();
            error = "Cannot write the drag copy";
            return false;
        }
        frame += count;
    }
    writer.reset();
    return destination.existsAsFile();
}
} // namespace

bool hasDragMetadata(const DragMetadata& metadata)
{
    return metadata.hasBpm || metadata.hasRoot;
}

bool canMaterialiseWithMetadata(const juce::File& source)
{
    const auto extension = extensionOf(source);
    return extension == ".wav" || extension == ".wave" || extension == ".aif"
        || extension == ".aiff" || extension == ".aifc" || extension == ".mp3"
        || extension == ".flac" || extension == ".ogg" || extension == ".caf"
        || extension == ".w64";
}

void pruneDragCache(const juce::File& cacheDir, const juce::File& keep, const int maxAgeDays)
{
    if (! cacheDir.isDirectory()) return;
    const auto cutoff = juce::Time::currentTimeMillis() - (juce::int64) maxAgeDays * 24 * 60 * 60 * 1000;
    juce::Array<juce::File> folders;
    cacheDir.findChildFiles(folders, juce::File::findDirectories, false);
    for (const auto& folder : folders)
    {
        if (folder == keep) continue;
        if (folder.getLastModificationTime().toMilliseconds() < cutoff)
            folder.deleteRecursively();
    }
}

DragMetadata dragMetadataForDisplay(const SmartDisplay& display)
{
    DragMetadata metadata;
    const bool bpmTrusted = display.bpmOverride == OverrideState::Value
        || display.bpmState == SmartState::Resolved;
    if (bpmTrusted && display.hasBpm && display.bpm > 0.0)
    {
        metadata.hasBpm = true;
        metadata.bpm = display.bpm;
    }
    const bool keyTrusted = display.keyOverride == OverrideState::Value
        || display.keyState == SmartState::Resolved;
    if (keyTrusted)
    {
        const auto root = rootMidiFromKey(display.key);
        if (root >= 0)
        {
            metadata.hasRoot = true;
            metadata.rootMidi = root;
        }
    }
    return metadata;
}

juce::File dragCopyPath(const juce::File& source, const DragMetadata& metadata,
                        const juce::File& cacheDir)
{
    auto stem = AudioExporter::sanitizeStem(source.getFileNameWithoutExtension());
    if (stem.isEmpty()) stem = "sample";
    return cacheDir.getChildFile(hashOf(source, metadata)).getChildFile(stem + ".wav");
}

juce::File materialiseWithMetadata(const juce::File& source, const DragMetadata& metadata,
                                   const juce::File& cacheDir, juce::String& error)
{
    error.clear();
    if (! hasDragMetadata(metadata)) return source;
    // Formats the shared decoder cannot read keep their previous drag behaviour rather than being
    // re-encoded or dropped.
    if (! canMaterialiseWithMetadata(source)) return source;
    if (! source.existsAsFile())
    {
        error = "File not found: " + source.getFileName();
        return {};
    }
    const auto destination = dragCopyPath(source, metadata, cacheDir);
    if (destination.existsAsFile()
        && destination.getLastModificationTime() >= source.getLastModificationTime())
    {
        pruneDragCache(cacheDir, destination.getParentDirectory());
        return destination;
    }
    pruneDragCache(cacheDir, destination.getParentDirectory());
    if (! destination.getParentDirectory().createDirectory())
    {
        error = "Cannot create the drag cache";
        return {};
    }
    destination.deleteFile();
    const auto extension = source.getFileExtension().toLowerCase();
    if (extension == ".wav" || extension == ".wave")
    {
        if (! source.copyFileTo(destination))
        {
            error = "Cannot copy " + source.getFileName();
            return {};
        }
    }
    else if (! encodeToWav(source, destination, error))
    {
        destination.deleteFile();
        return {};
    }
    const auto info = extractWavMetadata(destination).info;
    const auto duration = info.sampleRate > 0 ? (double) info.frames / info.sampleRate : 0.0;
    if (metadata.hasBpm)
    {
        const auto beats = duration > 0 ? (int) std::lround(duration * metadata.bpm / 60.0) : 0;
        if (! riff::insertOrReplaceChunk(destination, "acid",
                riff::buildAcid(metadata.bpm, beats, metadata.hasRoot ? metadata.rootMidi : -1)))
        {
            destination.deleteFile();
            error = "Cannot write the tempo chunk";
            return {};
        }
    }
    if (metadata.hasRoot)
    {
        juce::MemoryBlock existing;
        riff::readChunk(destination, "smpl", existing);
        if (! riff::insertOrReplaceChunk(destination, "smpl",
                riff::buildSmplRoot(metadata.rootMidi, existing)))
        {
            destination.deleteFile();
            error = "Cannot write the root chunk";
            return {};
        }
    }
    return destination;
}
} // namespace sonicator
