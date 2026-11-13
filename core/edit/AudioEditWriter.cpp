#include "AudioEditWriter.h"
#include "core/export/AudioExporter.h"
#include <cstring>
#include <filesystem>

#if JUCE_MAC
#include <sys/xattr.h>
#endif

namespace sonicator::edit
{
namespace
{
constexpr int chunkFrames = 65536;

juce::String extensionOf(const juce::File& file)
{
    return file.getFileExtension().toLowerCase();
}

std::unique_ptr<juce::AudioFormat> formatForExtension(const juce::String& extension)
{
    if (extension == ".aif" || extension == ".aiff") return std::make_unique<juce::AiffAudioFormat>();
    if (extension == ".flac") return std::make_unique<juce::FlacAudioFormat>();
    return std::make_unique<juce::WavAudioFormat>();
}

int writerBits(const AudioEditBuffer& buffer, bool floatingSupported)
{
    if (buffer.floatingPoint() && floatingSupported) return 32;
    return buffer.bitsPerSample() <= 16 ? 16 : 24;
}

juce::StringPairArray writerMetadata(const FileMarkers& markers, const juce::String& extension)
{
    juce::StringPairArray values;
    if (extension != ".wav" && extension != ".wave") return values;
    values.set("MidiUnityNote", juce::String(markers.rootKey));
    if (markers.hasLoop && markers.loopEndExclusive > markers.loopStart)
    {
        values.set("NumSampleLoops", "1");
        values.set("Loop0Start", juce::String(markers.loopStart));
        // JUCE stores the loop end as the last frame that plays.
        values.set("Loop0End", juce::String(markers.loopEndExclusive - 1));
        values.set("Loop0Type", "0");
    }
    if (! markers.cuePositions.empty())
    {
        values.set("NumCuePoints", juce::String((int) markers.cuePositions.size()));
        for (size_t i = 0; i < markers.cuePositions.size(); ++i)
        {
            values.set("Cue" + juce::String((int) i) + "Identifier", juce::String((int) i + 1));
            values.set("Cue" + juce::String((int) i) + "Order", juce::String((int) i));
            values.set("Cue" + juce::String((int) i) + "Offset",
                       juce::String(markers.cuePositions[i]));
        }
    }
    return values;
}

void copyFileAttributes(const juce::File& from, const juce::File& to)
{
#if ! JUCE_WINDOWS
    std::error_code error;
    const auto permissions = std::filesystem::status(from.getFullPathName().toStdString(), error)
                                 .permissions();
    if (! error)
        std::filesystem::permissions(to.getFullPathName().toStdString(), permissions,
                                     std::filesystem::perm_options::replace, error);
#endif
#if JUCE_MAC
    const auto sourcePath = from.getFullPathName();
    const auto destinationPath = to.getFullPathName();
    const auto size = listxattr(sourcePath.toRawUTF8(), nullptr, 0, 0);
    if (size <= 0) return;
    std::vector<char> names((size_t) size);
    if (listxattr(sourcePath.toRawUTF8(), names.data(), names.size(), 0) <= 0) return;
    for (const char* name = names.data(); name < names.data() + size; name += std::strlen(name) + 1)
    {
        const auto valueSize = getxattr(sourcePath.toRawUTF8(), name, nullptr, 0, 0, 0);
        if (valueSize < 0) continue;
        std::vector<char> value((size_t) valueSize);
        if (getxattr(sourcePath.toRawUTF8(), name, value.data(), value.size(), 0, 0) < 0) continue;
        setxattr(destinationPath.toRawUTF8(), name, value.data(), value.size(), 0, 0);
    }
#else
    juce::ignoreUnused(to);
#endif
}

SaveResult writeAudioFile(const juce::File& destination, const AudioEditBuffer& buffer,
                          const juce::String& extension, const bool replaceExisting,
                          CancelCheck cancel)
{
    if (buffer.frames() <= 0 || buffer.sampleRate() <= 0)
        return { false, destination, "Nothing to write" };
    if (! replaceExisting && destination.existsAsFile())
        return { false, destination, "Destination already exists: " + destination.getFileName() };
    destination.getParentDirectory().createDirectory();
    const int channels = buffer.channels();
    const bool floating = buffer.floatingPoint() && extension != ".aif" && extension != ".aiff";
    const int bits = writerBits(buffer, floating);
    auto format = formatForExtension(extension);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate((double) buffer.sampleRate())
        .withNumChannels(channels).withBitsPerSample(bits)
        .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                   : juce::AudioFormatWriterOptions::SampleFormat::integral);
    const auto metadata = writerMetadata(buffer.markers(), extension);
    for (int i = 0; i < metadata.size(); ++i) options = options.withMetadata(metadata.getAllKeys()[i],
                                                                             metadata.getAllValues()[i]);

    std::unique_ptr<juce::OutputStream> stream(destination.createOutputStream());
    if (stream == nullptr) return { false, destination, "Cannot create the file" };
    auto writer = format->createWriterFor(stream, options);
    if (writer == nullptr) return { false, destination, "The format rejected the audio settings" };

    juce::AudioBuffer<float> chunk(channels, chunkFrames);
    int64_t frame = 0;
    while (frame < buffer.frames())
    {
        if (cancel && cancel()) return { false, destination, "Cancelled" };
        const auto count = (int) juce::jmin<int64_t>(chunkFrames, buffer.frames() - frame);
        chunk.copyFrom(0, 0, buffer.channel(0).data() + frame, count);
        if (channels > 1) chunk.copyFrom(1, 0, buffer.channel(1).data() + frame, count);
        if (! writer->writeFromFloatArrays(chunk.getArrayOfReadPointers(), channels, count))
            return { false, destination, "Cannot write the audio" };
        frame += count;
    }
    // Only the WAV writer overrides flush; the base implementation returns false because other
    // formats finalise in their destructor.
    const bool needsFlush = extension == ".wav" || extension == ".wave";
    if (needsFlush && ! writer->flush()) return { false, destination, "Cannot finalise the file" };
    writer.reset();
    stream.reset();
    if (extension == ".wav" || extension == ".wave")
    {
        const auto validation = AudioExporter::validateWavOutput(destination, buffer.frames(),
            (double) buffer.sampleRate(), channels, bits, floating);
        if (validation.isNotEmpty()) return { false, destination, validation };
    }
    return { true, destination, {} };
}
} // namespace

bool canOverwriteInPlace(const juce::File& target)
{
    const auto extension = extensionOf(target);
    return extension == ".wav" || extension == ".wave" || extension == ".aif"
        || extension == ".aiff" || extension == ".flac";
}

juce::File uniqueCopyPath(const juce::File& original, const juce::String& stem)
{
    const auto extension = canOverwriteInPlace(original) ? original.getFileExtension() : ".wav";
    auto candidate = original.getSiblingFile(stem + " edit" + extension);
    for (int suffix = 2; candidate.existsAsFile(); ++suffix)
        candidate = original.getSiblingFile(stem + " edit " + juce::String(suffix) + extension);
    return candidate;
}

SaveResult overwrite(const juce::File& target, const AudioEditBuffer& buffer, CancelCheck cancel)
{
    if (! canOverwriteInPlace(target))
        return { false, target, "This format cannot be overwritten; save a copy instead" };
    if (! target.existsAsFile()) return { false, target, "File not found" };
    if (cancel && cancel()) return { false, target, "Cancelled" };
    const auto writeTarget = target.isSymbolicLink() ? target.getLinkedTarget() : target;
    juce::TemporaryFile temporary(writeTarget);
    auto result = writeAudioFile(temporary.getFile(), buffer, extensionOf(writeTarget), true, cancel);
    if (! result.success)
    {
        temporary.getFile().deleteFile();
        return { false, target, result.error };
    }
    copyFileAttributes(writeTarget, temporary.getFile());
    if (! temporary.overwriteTargetFileWithTemporary())
        return { false, target, "Cannot replace the file" };
    return { true, writeTarget, {} };
}

SaveResult saveCopy(const juce::File& original, const juce::String& stem,
                    const AudioEditBuffer& buffer, CancelCheck cancel)
{
    if (cancel && cancel()) return { false, {}, "Cancelled" };
    const auto destination = uniqueCopyPath(original, stem);
    auto result = writeAudioFile(destination, buffer, extensionOf(destination), false, cancel);
    if (! result.success) result.file.deleteFile();
    return result;
}
} // namespace sonicator::edit
