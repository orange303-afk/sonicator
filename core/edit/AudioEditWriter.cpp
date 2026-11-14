#include "AudioEditWriter.h"
#include "AudioFileWriter.h"

namespace sonicator::edit
{
namespace
{
constexpr int chunkFrames = 65536;

juce::String extensionOf(const juce::File& file)
{
    return file.getFileExtension().toLowerCase();
}

bool isSamplerKey(const juce::String& key)
{
    return key.startsWith("Loop") || key == "NumSampleLoops" || key == "MidiUnityNote"
        || key == "SamplerData" || key == "Manufacturer" || key == "Product"
        || key == "SamplePeriod" || key == "MidiPitchFraction" || key == "SmpteFormat"
        || key == "SmpteOffset" || key == "NumCuePoints" || key.startsWith("Cue");
}

juce::StringPairArray writerMetadata(const AudioEditBuffer& buffer, const juce::String& extension)
{
    const auto& markers = buffer.markers();
    juce::StringPairArray values;
    const auto& source = buffer.sourceMetadata();
    for (int i = 0; i < source.size(); ++i)
        if (! isSamplerKey(source.getAllKeys()[i]))
            values.set(source.getAllKeys()[i], source.getAllValues()[i]);
    if (extension != ".wav" && extension != ".wave") return values;
    if (markers.hasRootKey) values.set("MidiUnityNote", juce::String(markers.rootKey));
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

SaveResult writeAudioFile(const juce::File& destination, const AudioEditBuffer& buffer,
                          const juce::String& extension, const bool replaceExisting,
                          CancelCheck cancel)
{
    if (buffer.frames() <= 0 || buffer.sampleRate() <= 0)
        return { false, destination, "Nothing to write" };
    if (! replaceExisting && destination.existsAsFile())
        return { false, destination, "Destination already exists: " + destination.getFileName() };

    AudioWriteSpec spec;
    spec.sampleRate = buffer.sampleRate();
    spec.channels = buffer.channels();
    spec.bitsPerSample = buffer.bitsPerSample();
    spec.frames = buffer.frames();
    spec.floatingPoint = buffer.floatingPoint();
    spec.metadata = writerMetadata(buffer, extension);
    return writeAudioStream(destination, extension, spec,
        [&buffer, cancel, channels = spec.channels](juce::AudioFormatWriter& writer)
        {
            juce::AudioBuffer<float> chunk(channels, chunkFrames);
            int64_t frame = 0;
            while (frame < buffer.frames())
            {
                if (cancel && cancel()) return false;
                const auto count = (int) juce::jmin<int64_t>(chunkFrames, buffer.frames() - frame);
                chunk.copyFrom(0, 0, buffer.channel(0).data() + frame, count);
                if (channels > 1)
                    chunk.copyFrom(1, 0, buffer.channel(1).data() + frame, count);
                if (! writer.writeFromFloatArrays(chunk.getArrayOfReadPointers(), channels, count))
                    return false;
                frame += count;
            }
            return true;
        }, cancel);
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
