#include "AudioExporter.h"
#include <cmath>
namespace sonicator {
namespace
{
ExportResult failure(const juce::File& part, juce::String message)
{
    part.deleteFile();
    return { false, {}, std::move(message) };
}
bool isCompressedFormatName(const juce::String& name)
{
    return name.containsIgnoreCase("FLAC") || name.containsIgnoreCase("MP3");
}
}
juce::File AudioExporter::defaultExportFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/exports");
}
juce::String AudioExporter::sanitizeStem(const juce::String& name)
{
    juce::String result;
    for (auto c : name)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_') result << juce::String::charToString(c);
        else result << '_';
    }
    while (result.contains("__")) result = result.replace("__", "_");
    result = result.trimCharactersAtStart("._ ").trimCharactersAtEnd("._ ");
    if (result.isEmpty()) result = "audio";
    return result.substring(0, 48);
}
juce::String AudioExporter::rangeSuffix(double startSeconds, double endSeconds)
{
    const auto milliseconds = [] (double seconds)
    {
        return juce::String((int) std::llround(seconds * 1000.0)).paddedLeft('0', 4);
    };
    return milliseconds(startSeconds) + "-" + milliseconds(endSeconds);
}
ExportResult AudioExporter::exportRange(const juce::File& source, double start, double end,
                                        const juce::File& destination, CancelCheck cancel) const
{
    const auto part = juce::File(destination.getFullPathName() + ".part");
    if (! std::isfinite(start) || ! std::isfinite(end))
        return failure(part, "The selection range is not a finite time");
    if (! source.existsAsFile()) return failure(part, "Cannot open source audio file: " + source.getFileName());
    if (! destination.getParentDirectory().createDirectory())
        return failure(part, "Cannot create export folder: " + destination.getParentDirectory().getFullPathName());
    if (destination.existsAsFile()) return failure(part, "Export file already exists: " + destination.getFileName());
    try
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
        if (reader == nullptr) return failure(part, "Cannot decode source audio: " + source.getFileName());
        const auto rate = reader->sampleRate;
        const int channels = (int) reader->numChannels;
        if (! (rate > 0) || ! std::isfinite(rate))
            return failure(part, "Source has an invalid sample rate: " + source.getFileName());
        if (channels < 1 || channels > maxChannels)
            return failure(part, "Unsupported channel count: " + juce::String(channels)
                + " (maximum " + juce::String(maxChannels) + ")");
        if (reader->lengthInSamples <= 0)
            return failure(part, "Source has no audio frames: " + source.getFileName());
        auto startFrame = (juce::int64) std::llround(start * rate);
        auto endFrame = (juce::int64) std::llround(end * rate);
        startFrame = juce::jlimit<juce::int64>(0, reader->lengthInSamples, startFrame);
        endFrame = juce::jlimit<juce::int64>(0, reader->lengthInSamples, endFrame);
        if (endFrame <= startFrame)
            return failure(part, "The selection is shorter than one audio frame");
        const bool floatingOutput = reader->usesFloatingPointData;
        int bits = 24;
        if (floatingOutput) bits = 32;
        else if (isCompressedFormatName(reader->getFormatName())) bits = 24;
        else if (reader->bitsPerSample <= 16) bits = 16;
        part.deleteFile();
        std::unique_ptr<juce::OutputStream> stream(part.createOutputStream());
        if (stream == nullptr) return failure(part, "Cannot create export file: " + destination.getFileName());
        juce::WavAudioFormat wav;
        auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate)
            .withNumChannels(channels).withBitsPerSample(bits)
            .withSampleFormat(floatingOutput ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                             : juce::AudioFormatWriterOptions::SampleFormat::integral);
        auto writer = wav.createWriterFor(stream, options);
        if (writer == nullptr)
        {
            stream.reset();
            return failure(part, "WAV writer rejected " + juce::String(channels) + " channels at "
                + juce::String(bits) + " bits");
        }
        juce::AudioBuffer<float> buffer(channels, (int) juce::jmin<juce::int64>(exportChunkFrames, endFrame - startFrame));
        auto frame = startFrame;
        while (frame < endFrame)
        {
            if (cancel && cancel()) return failure(part, "Export cancelled");
            const auto remaining = endFrame - frame;
            const int count = (int) juce::jmin<juce::int64>(exportChunkFrames, remaining);
            buffer.setSize(channels, count, false, false, true);
            if (! reader->read(buffer.getArrayOfWritePointers(), channels, frame, count))
                return failure(part, "Cannot read source audio: " + source.getFileName());
            if (! writer->writeFromFloatArrays(buffer.getArrayOfReadPointers(), channels, count))
                return failure(part, "Cannot write export audio: " + destination.getFileName());
            frame += count;
        }
        writer->flush();
        writer.reset();
        if (! part.existsAsFile() || part.getSize() <= 0)
            return failure(part, "Cannot finalise export file: " + destination.getFileName());
        if (! part.moveFileTo(destination))
            return failure(part, "Cannot finalise export file: " + destination.getFileName());
        return { true, destination, {} };
    }
    catch (const std::exception& e)
    {
        return failure(part, juce::String::fromUTF8(e.what()));
    }
    catch (...)
    {
        return failure(part, "Unknown export error");
    }
}
ExportResult AudioExporter::exportSelection(const juce::File& source, double start, double end,
                                            const juce::File& folder, CancelCheck cancel) const
{
    if (! folder.createDirectory())
        return { false, {}, "Cannot create export folder: " + folder.getFullPathName() };
    const auto base = sanitizeStem(source.getFileNameWithoutExtension()) + "_" + rangeSuffix(start, end) + "_";
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        const auto suffix = juce::Uuid().toString().replace("-", "").substring(0, 6).toLowerCase();
        const auto destination = folder.getChildFile(base + suffix + ".wav");
        const auto part = juce::File(destination.getFullPathName() + ".part");
        if (destination.existsAsFile() || part.existsAsFile()) continue;
        return exportRange(source, start, end, destination, cancel);
    }
    return { false, {}, "Cannot allocate a unique export file name" };
}
} // namespace sonicator
