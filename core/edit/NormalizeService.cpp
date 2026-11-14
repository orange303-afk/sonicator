#include "NormalizeService.h"
#include "AudioEditWriter.h"
#include "AudioFileWriter.h"
#include "core/audio/SonicatorFormats.h"
#include <algorithm>
#include <cmath>

namespace sonicator
{
namespace
{
constexpr int chunkFrames = 65536;
// -120 dBFS: below this the file has no audio worth scaling and the design asks for "no change".
constexpr float silentPeak = 1.0e-6f;
// A gain this close to unity (under 0.005 dB) would only rewrite identical bytes.
constexpr float unityGainTolerance = 5.0e-4f;

juce::String extensionOf(const juce::File& file)
{
    return file.getFileExtension().toLowerCase();
}

// The source metadata is passed through unchanged: Normalize moves no markers, so a smpl loop,
// cue points, INFO or bext tags keep their original values. `MetaDataSource` describes the file
// the values came from and must not be copied into a WAV.
juce::StringPairArray metadataFor(const juce::StringPairArray& source, const juce::String& extension)
{
    juce::StringPairArray values;
    for (int i = 0; i < source.size(); ++i)
    {
        const auto& key = source.getAllKeys()[i];
        if (extension == ".wav" || extension == ".wave")
            if (key == "MetaDataSource") continue;
        values.set(key, source.getAllValues()[i]);
    }
    return values;
}

NormalizeFileResult unchanged(NormalizeFileResult result, const juce::String& reason, float peak)
{
    result.state = NormalizeFileState::unchanged;
    result.peak = peak;
    result.error = reason;
    return result;
}
} // namespace

juce::String normalizeSkipReason(const juce::File& file)
{
    if (! file.existsAsFile()) return "File is missing";
    if (! FileScanner::isSupportedFormat(file) || FileScanner::isMetadataOnlyFormat(file))
        return "Format cannot be normalized";
    return {};
}

bool normalizeMeasurePeak(const juce::File& source, int& channels, float& peak, juce::String& error)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
    {
        error = "Cannot decode the source";
        return false;
    }
    channels = (int) reader->numChannels;
    juce::AudioBuffer<float> buffer(channels, chunkFrames);
    peak = 0.0f;
    for (int64_t position = 0; position < reader->lengthInSamples;)
    {
        const auto count = (int) juce::jmin<int64_t>(chunkFrames, reader->lengthInSamples - position);
        if (! reader->read(&buffer, 0, count, position, true, true))
        {
            error = "Cannot read the source";
            return false;
        }
        peak = juce::jmax(peak, buffer.getMagnitude(0, count));
        position += count;
    }
    return true;
}

NormalizeFileResult normalizeFile(const juce::File& source, const NormalizeOptions& options,
                                  CancelCheck cancel)
{
    NormalizeFileResult result;
    result.source = source;
    const auto cancelled = [&cancel]
    {
        return cancel && cancel();
    };
    const auto skip = normalizeSkipReason(source);
    if (skip.isNotEmpty())
    {
        result.state = NormalizeFileState::skipped;
        result.error = skip;
        return result;
    }
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
    {
        result.state = NormalizeFileState::skipped;
        result.error = "Cannot decode the source";
        return result;
    }
    if (reader->numChannels <= 0)
    {
        result.state = NormalizeFileState::skipped;
        result.error = "The source has no channels";
        return result;
    }
    if (reader->numChannels > 2)
    {
        result.state = NormalizeFileState::skipped;
        result.error = "More than two channels";
        return result;
    }
    const int channels = (int) reader->numChannels;
    const int64_t frames = reader->lengthInSamples;
    const int sampleRate = (int) reader->sampleRate;
    const auto sourceMetadata = reader->metadataValues;
    const bool floatingPoint = reader->usesFloatingPointData;
    const int bitsPerSample = reader->bitsPerSample;
    juce::AudioBuffer<float> buffer(channels, chunkFrames);
    float peak = 0.0f;
    bool invalidSample = false;
    for (int64_t position = 0; position < frames && ! invalidSample;)
    {
        if (cancelled())
        {
            result.state = NormalizeFileState::cancelled;
            result.error = "Cancelled";
            return result;
        }
        const auto count = (int) juce::jmin<int64_t>(chunkFrames, frames - position);
        if (! reader->read(&buffer, 0, count, position, true, true))
        {
            result.state = NormalizeFileState::failed;
            result.error = "Cannot read the source";
            return result;
        }
        // getMagnitude treats NaN comparisons as "not larger", so a NaN would otherwise read as a
        // silent file; only float sources can carry one, so integral files skip the scan.
        if (floatingPoint)
            for (int channel = 0; channel < channels && ! invalidSample; ++channel)
            {
                const auto* samples = buffer.getReadPointer(channel);
                for (int i = 0; i < count; ++i)
                    if (! std::isfinite(samples[i])) { invalidSample = true; break; }
            }
        peak = juce::jmax(peak, buffer.getMagnitude(0, count));
        position += count;
    }
    reader.reset();
    result.peak = peak;
    if (invalidSample)
    {
        result.state = NormalizeFileState::failed;
        result.error = "The source contains an invalid sample";
        return result;
    }
    if (! std::isfinite(peak))
    {
        // A NaN or infinite sample would make the gain non-finite and silently write broken audio
        // over the original, while the length check would still pass.
        result.state = NormalizeFileState::failed;
        result.error = "The source contains an invalid sample";
        return result;
    }
    if (peak <= silentPeak) return unchanged(result, "Silent source", peak);
    const float gain = normalizePeakTarget / peak;
    result.gain = gain;
    if (! std::isfinite(gain))
    {
        result.state = NormalizeFileState::failed;
        result.error = "The source contains an invalid sample";
        return result;
    }
    if (std::abs(gain - 1.0f) <= unityGainTolerance)
        return unchanged(result, "Already at -0.1 dBFS", peak);

    const auto originalSize = source.getSize();
    const auto originalModified = source.getLastModificationTime().toMilliseconds();
    const auto writeTarget = source.isSymbolicLink() ? source.getLinkedTarget() : source;
    // The decision uses the resolved file: a .mp3 link to a .wav target can be overwritten.
    const bool overwrite = options.overwriteOriginals && edit::canOverwriteInPlace(writeTarget);
    const auto sourceExtension = extensionOf(writeTarget);
    const auto extension = overwrite ? sourceExtension : juce::String(".wav");
    // A decoded lossy source reports float data, but a sample library wants an integral WAV.
    const bool sourceIsPcm = sourceExtension == ".wav" || sourceExtension == ".wave"
        || sourceExtension == ".aif" || sourceExtension == ".aiff" || sourceExtension == ".flac";

    edit::AudioWriteSpec spec;
    spec.sampleRate = sampleRate;
    spec.channels = channels;
    spec.bitsPerSample = overwrite || sourceIsPcm ? bitsPerSample : 24;
    spec.frames = frames;
    spec.floatingPoint = overwrite || sourceIsPcm ? floatingPoint : false;
    spec.metadata = metadataFor(sourceMetadata, extension);

    const auto feed = [&](juce::AudioFormatWriter& writer)
    {
        std::unique_ptr<juce::AudioFormatReader> second(formats.createReaderFor(source));
        if (second == nullptr) return false;
        for (int64_t position = 0; position < frames;)
        {
            if (cancelled()) return false;
            const auto count = (int) juce::jmin<int64_t>(chunkFrames, frames - position);
            buffer.clear();
            if (! second->read(&buffer, 0, count, position, true, true)) return false;
            // One coefficient for every channel: the stereo image does not move.
            buffer.applyGain(0, count, gain);
            if (! writer.writeFromFloatArrays(buffer.getArrayOfWritePointers(), channels, count))
                return false;
            position += count;
        }
        return true;
    };

    if (overwrite)
    {
        juce::TemporaryFile temporary(writeTarget);
        auto written = edit::writeAudioStream(temporary.getFile(), extension, spec, feed, cancel);
        if (! written.success)
        {
            temporary.getFile().deleteFile();
            result.state = cancelled() ? NormalizeFileState::cancelled : NormalizeFileState::failed;
            result.error = written.error;
            return result;
        }
        if (source.getSize() != originalSize
            || source.getLastModificationTime().toMilliseconds() != originalModified)
        {
            temporary.getFile().deleteFile();
            result.state = NormalizeFileState::failed;
            result.error = "The file changed while it was being normalized";
            return result;
        }
        edit::copyFileAttributes(writeTarget, temporary.getFile());
        if (! temporary.overwriteTargetFileWithTemporary())
        {
            result.state = NormalizeFileState::failed;
            result.error = "Cannot replace the file";
            return result;
        }
        result.destination = writeTarget;
        result.replacedOriginal = true;
    }
    else
    {
        const auto destination = edit::uniqueCopyPath(source, source.getFileNameWithoutExtension());
        auto written = edit::writeAudioStream(destination, extension, spec, feed, cancel);
        if (! written.success)
        {
            result.state = cancelled() ? NormalizeFileState::cancelled : NormalizeFileState::failed;
            result.error = written.error;
            return result;
        }
        result.destination = destination;
    }
    result.state = NormalizeFileState::normalized;
    return result;
}

NormalizeService::~NormalizeService()
{
    cancel();
    waitForThreadToExit(-1);
}

bool NormalizeService::start(const std::vector<juce::File>& sources, const NormalizeOptions& next)
{
    // The previous run may still be unwinding after it published `running = false`; wait for it
    // instead of rejecting a fresh batch in that tiny window.
    if (isThreadRunning() && ! waitForThreadToExit(2000)) return false;
    {
        std::lock_guard lock(stateMutex);
        files = sources;
        options = next;
        state = {};
        state.running = true;
        state.total = (int) files.size();
        state.message = "Normalizing";
        fileResults.clear();
    }
    cancelled = false;
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.message = "Cannot start the normalize thread";
        return false;
    }
    return true;
}

void NormalizeService::cancel()
{
    cancelled = true;
}

NormalizeProgress NormalizeService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}

std::vector<NormalizeFileResult> NormalizeService::results() const
{
    std::lock_guard lock(stateMutex);
    return fileResults;
}

void NormalizeService::run()
{
    const auto cancelCheck = [this] { return cancelled.load(); };
    for (const auto& source : files)
    {
        if (cancelled)
        {
            NormalizeFileResult skipped;
            skipped.source = source;
            skipped.state = NormalizeFileState::cancelled;
            skipped.error = "Cancelled";
            std::lock_guard lock(stateMutex);
            ++state.processed;
            fileResults.push_back(skipped);
            break;
        }
        {
            std::lock_guard lock(stateMutex);
            state.current = source.getFileName();
        }
        auto result = normalizeFile(source, options, cancelCheck);
        {
            std::lock_guard lock(stateMutex);
            ++state.processed;
            switch (result.state)
            {
                case NormalizeFileState::normalized: ++state.normalized; break;
                case NormalizeFileState::unchanged: ++state.unchanged; break;
                case NormalizeFileState::failed: ++state.failed; break;
                default: ++state.skipped; break;
            }
            fileResults.push_back(result);
        }
        if (result.state == NormalizeFileState::cancelled) break;
    }
    std::lock_guard lock(stateMutex);
    state.running = false;
    state.cancelled = cancelled;
    state.message = cancelled ? "Normalize cancelled" : "Normalize complete";
}
} // namespace sonicator
