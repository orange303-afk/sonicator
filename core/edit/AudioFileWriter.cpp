#include "AudioFileWriter.h"
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
std::unique_ptr<juce::AudioFormat> formatForExtension(const juce::String& extension)
{
    if (extension == ".aif" || extension == ".aiff") return std::make_unique<juce::AiffAudioFormat>();
    if (extension == ".flac") return std::make_unique<juce::FlacAudioFormat>();
    return std::make_unique<juce::WavAudioFormat>();
}

juce::String describeDifference(const juce::String& what, double expected, double actual)
{
    return what + " is " + juce::String(actual, 2) + " instead of " + juce::String(expected, 2);
}

juce::String validateWrittenFile(const juce::File& file, const juce::String& extension,
                                 const AudioWriteSpec& spec, int bits, bool floating)
{
    if (extension == ".wav" || extension == ".wave")
        return AudioExporter::validateWavOutput(file, spec.frames, (double) spec.sampleRate,
                                                spec.channels, bits, floating);
    // AIFF and FLAC finalise in the writer's destructor, so the check runs after it: a truncated
    // or rejected stream would otherwise look like a successful save.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) return "The written file cannot be decoded";
    if (reader->lengthInSamples != spec.frames)
        return describeDifference("The written length", (double) spec.frames,
                                  (double) reader->lengthInSamples);
    if ((int) reader->sampleRate != spec.sampleRate)
        return describeDifference("The written sample rate", spec.sampleRate, reader->sampleRate);
    if ((int) reader->numChannels != spec.channels)
        return describeDifference("The written channel count", spec.channels, reader->numChannels);
    return {};
}
} // namespace

SaveResult writeAudioStream(const juce::File& destination, const juce::String& extension,
                            const AudioWriteSpec& spec, const AudioFeed& feed, CancelCheck cancel)
{
    if (spec.frames <= 0 || spec.sampleRate <= 0 || spec.channels <= 0)
        return { false, destination, "Nothing to write" };
    if (! feed)
        return { false, destination, "Nothing to write" };
    if (destination.existsAsFile())
        return { false, destination, "Destination already exists: " + destination.getFileName() };
    if (cancel && cancel()) return { false, destination, "Cancelled" };
    destination.getParentDirectory().createDirectory();
    const bool floating = spec.floatingPoint && extension != ".aif" && extension != ".aiff";
    const int bits = floating ? 32 : (spec.bitsPerSample <= 16 ? 16 : 24);
    auto format = formatForExtension(extension);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate((double) spec.sampleRate)
        .withNumChannels(spec.channels).withBitsPerSample(bits)
        .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                   : juce::AudioFormatWriterOptions::SampleFormat::integral);
    for (int i = 0; i < spec.metadata.size(); ++i)
        options = options.withMetadata(spec.metadata.getAllKeys()[i],
                                       spec.metadata.getAllValues()[i]);

    std::unique_ptr<juce::OutputStream> stream(destination.createOutputStream());
    if (stream == nullptr) return { false, destination, "Cannot create the file" };
    auto writer = format->createWriterFor(stream, options);
    if (writer == nullptr) return { false, destination, "The format rejected the audio settings" };

    if (! feed(*writer))
    {
        const bool wasCancelled = cancel && cancel();
        writer.reset();
        stream.reset();
        destination.deleteFile();
        return { false, destination, wasCancelled ? "Cancelled" : "Cannot write the audio" };
    }
    // Only the WAV writer overrides flush; the base implementation returns false because other
    // formats finalise in their destructor.
    const bool needsFlush = extension == ".wav" || extension == ".wave";
    if (needsFlush && ! writer->flush())
    {
        writer.reset();
        stream.reset();
        destination.deleteFile();
        return { false, destination, "Cannot finalise the file" };
    }
    writer.reset();
    stream.reset();
    const auto validation = validateWrittenFile(destination, extension, spec, bits, floating);
    if (validation.isNotEmpty())
    {
        destination.deleteFile();
        return { false, destination, validation };
    }
    return { true, destination, {} };
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
} // namespace sonicator::edit
