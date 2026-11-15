#include "core/match/OfflineMatchRenderer.h"
#include "core/match/MatchPolicy.h"
#include "core/audio/SonicatorFormats.h"
#include "core/export/AudioExporter.h"
#include "rubberband/RubberBandStretcher.h"
#include <cmath>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace sonicator
{
namespace
{
constexpr int64_t blockFrames = 8192;
constexpr int64_t maximumSourceFrames = 44100 * 60 * 10 + 1; // ten minutes at 44.1 kHz
constexpr int outputBits = 24;

juce::String failureText (const juce::String& text) { return text; }

bool publishNoClobber (const juce::File& partial, const juce::File& destination)
{
    if (destination.existsAsFile()) return false;
#if JUCE_WINDOWS
    return MoveFileExW (partial.getFullPathName().toWideCharPointer(),
                        destination.getFullPathName().toWideCharPointer(), 0) != 0;
#else
    // link() is atomic and fails when the destination appears between the check and the call.
    if (::link (partial.getFullPathName().toRawUTF8(),
                destination.getFullPathName().toRawUTF8()) != 0)
        return false;
    partial.deleteFile(); // the published name now owns the same inode
    return true;
#endif
}

struct SourceFingerprint
{
    int64_t size = 0, modified = 0;
};

SourceFingerprint fingerprintOf (const juce::File& file)
{
    return { file.getSize(), file.getLastModificationTime().toMilliseconds() };
}

bool sameFingerprint (const SourceFingerprint& a, const SourceFingerprint& b)
{
    return a.size == b.size && a.modified == b.modified;
}

bool buildMetadata (juce::AudioFormatWriterOptions& options, const MatchRenderRequest& request)
{
    // A sampler root belongs to the source; a pitch-shifted result moved it by the same interval.
    // A stale loop or slice offset from the original would now point at the wrong audio, so none
    // is copied: the output stays a plain result WAV.
    if (request.source.rootNote >= 0 && request.source.rootState == MetadataState::known)
    {
        const int moved = juce::jlimit (0, 127, request.source.rootNote
                                              + (int) std::llround (request.plan.semitones));
        options = options.withMetadata ("MidiUnityNote", juce::String (moved));
    }
    return true;
}

bool finiteAudio (const juce::AudioBuffer<float>& buffer, int count)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* samples = buffer.getReadPointer (channel);
        for (int i = 0; i < count; ++i)
            if (! std::isfinite (samples[i])) return false;
    }
    return true;
}

void readIntoBuffer (juce::AudioFormatReader& reader, juce::AudioBuffer<float>& buffer,
                     int64_t position, int count, int channels)
{
    buffer.clear();
    reader.read (&buffer, 0, count, position, true, channels > 1);
}
} // namespace

juce::File matchExportRoot()
{
    return AudioExporter::defaultExportFolder().getChildFile ("match");
}

juce::String matchAlgorithmVersion()
{
    return "rubberband-3.3.0-r3-offline-builtin-fft-v1";
}

juce::String makeMatchResultKey (const MatchSource& source, const MatchPlan& plan,
                                 int64_t sourceSize, int64_t sourceModified)
{
    juce::String material;
    material << matchAlgorithmVersion() << '|'
             << source.identity << '|'
             << juce::String (sourceSize) << '|' << juce::String (sourceModified) << '|'
             << juce::String (source.metadataRevision) << '|'
             << juce::String (source.startFrame) << '|' << juce::String (source.sourceFrames) << '|'
             << juce::String (source.sampleRate, 6) << '|' << juce::String (source.channels) << '|'
             << juce::String ((int) source.structure) << '|'
             << juce::String (source.bpm, 6) << '|' << juce::String ((int) source.bpmState) << '|'
             << juce::String (source.keyTonic) << '|' << source.keyMode << '|'
             << juce::String ((int) source.keyState) << '|'
             << juce::String (plan.timeRatio, 9) << '|' << juce::String (plan.semitones, 9) << '|'
             << juce::String (plan.targetFrames) << '|'
             << juce::String (plan.targetTonic) << '|' << targetModeName (plan.targetMode);
    return juce::SHA256 (material.toRawUTF8(), (size_t) material.getNumBytesAsUTF8())
        .toHexString().substring (0, 40);
}

juce::File matchOutputFile (const juce::String& key, const juce::String& sourceStem,
                            const juce::File& root)
{
    auto stem = AudioExporter::sanitizeStem (sourceStem);
    if (stem.isEmpty()) stem = "sample";
    const auto base = root == juce::File() ? matchExportRoot() : root;
    return base.getChildFile (key).getChildFile (stem + "-match.wav");
}

bool validateMatchOutput (const juce::File& file, int64_t expectedFrames, double sampleRate,
                          int channels, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Matched WAV is missing";
        return false;
    }
    juce::AudioFormatManager formats;
    registerSonicatorFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Matched WAV cannot be reopened";
        return false;
    }
    const auto problem = AudioExporter::validateWavOutput (file, expectedFrames, sampleRate,
                                                           channels, outputBits, false);
    if (problem.isNotEmpty())
    {
        error = problem;
        return false;
    }
    return true;
}

MatchRenderResult renderMatch (const MatchRenderRequest& request, CancelCheck cancel)
{
    MatchRenderResult result;
    result.targetTonic = request.plan.targetTonic;
    result.targetMode = targetModeName (request.plan.targetMode);

    if (request.plan.action != MatchAction::transform)
    {
        result.error = "Match plan is not a transform";
        return result;
    }
    if (! request.sourceFile.existsAsFile())
    {
        result.error = "Source file is missing";
        return result;
    }
    if (request.plan.targetFrames <= 0 || ! std::isfinite (request.plan.timeRatio)
        || ! std::isfinite (request.plan.semitones))
    {
        result.error = "Match plan has invalid parameters";
        return result;
    }
    if (request.outputFile == juce::File() || request.outputFile.getParentDirectory() == juce::File())
    {
        result.error = "Matched output path is empty";
        return result;
    }

    juce::AudioFormatManager formats;
    registerSonicatorFormats (formats);
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (request.sourceFile));
    if (reader == nullptr)
    {
        result.error = "Source audio cannot be read";
        return result;
    }
    const int channels = (int) reader->numChannels;
    if (channels < 1 || channels > 2)
    {
        result.error = "Only mono and stereo sources are supported for Match";
        return result;
    }
    const double sampleRate = reader->sampleRate;
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0)
    {
        result.error = "Source sample rate is not usable";
        return result;
    }
    if (request.source.sourceFrames <= 0
        || request.source.sourceFrames > maximumSourceFrames)
    {
        result.error = "Source is longer than the ten minute Match limit";
        return result;
    }
    if (request.source.startFrame < 0
        || request.source.startFrame + request.source.sourceFrames > reader->lengthInSamples)
    {
        result.error = "Source range is outside the file";
        return result;
    }

    const auto before = fingerprintOf (request.sourceFile);
    const auto expected = request.sourceSize >= 0
        ? SourceFingerprint { request.sourceSize, request.sourceModified } : before;
    if (! sameFingerprint (before, expected))
    {
        result.error = "Source changed since Match was requested";
        return result;
    }

    // Reuse a published file only after checking its content, not only its name.
    if (request.outputFile.existsAsFile())
    {
        juce::String error;
        if (validateMatchOutput (request.outputFile, request.plan.targetFrames, sampleRate,
                                 channels, error))
        {
            result.success = true;
            result.file = request.outputFile;
            result.frames = request.plan.targetFrames;
            result.sampleRate = sampleRate;
            result.channels = channels;
            return result;
        }
        result.error = "Published matched WAV is corrupt: " + error;
        return result;
    }

    if (! request.outputFile.getParentDirectory().createDirectory())
    {
        result.error = "Cannot create matched output folder";
        return result;
    }

    // The partial keeps a .wav extension so the format manager can reopen it for validation.
    const auto partial = request.outputFile.getSiblingFile (
        request.outputFile.getFileNameWithoutExtension() + ".partial-"
        + juce::String::toHexString (juce::Time::getHighResolutionTicks()) + ".wav");
    std::unique_ptr<juce::OutputStream> stream;
    if (request.openStream)
        stream = request.openStream (partial);
    else
        stream.reset (partial.createOutputStream().release());
    if (stream == nullptr)
    {
        result.error = "Cannot create matched WAV";
        return result;
    }

    const int64_t targetFrames = request.plan.targetFrames;
    auto options = juce::AudioFormatWriterOptions {}
        .withSampleRate (sampleRate)
        .withNumChannels (channels)
        .withBitsPerSample (outputBits)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::integral);
    buildMetadata (options, request);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
    {
        stream.reset();
        partial.deleteFile();
        result.error = "WAV writer rejected the matched output";
        return result;
    }

    using Stretcher = RubberBand::RubberBandStretcher;
    const int stretcherOptions = Stretcher::OptionProcessOffline
        | Stretcher::OptionEngineFiner
        | Stretcher::OptionChannelsTogether
        | Stretcher::OptionPitchHighQuality;
    std::unique_ptr<Stretcher> stretcher;
    try
    {
        stretcher = std::make_unique<Stretcher> ((size_t) std::llround (sampleRate), (size_t) channels,
                                                 stretcherOptions, request.plan.timeRatio,
                                                 semitoneRatio ((int) std::llround (request.plan.semitones)));
    }
    catch (...)
    {
        writer.reset();
        partial.deleteFile();
        result.error = "The time/pitch engine could not be started";
        return result;
    }
    stretcher->setMaxProcessSize ((size_t) juce::jmin (blockFrames, request.source.sourceFrames));
    stretcher->setExpectedInputDuration ((size_t) request.source.sourceFrames);

    juce::AudioBuffer<float> input (channels, (int) juce::jmin (blockFrames, request.source.sourceFrames));
    const bool stereo = channels > 1;

    // Pass one: study the whole range. Nothing is written yet, so a cancelled study leaves no
    // partial file behind.
    for (int64_t offset = 0; offset < request.source.sourceFrames;)
    {
        if (cancel && cancel())
        {
            writer.reset();
            partial.deleteFile();
            result.cancelled = true;
            result.error = "Match cancelled";
            return result;
        }
        const int count = (int) juce::jmin<int64_t> (input.getNumSamples(), request.source.sourceFrames - offset);
        readIntoBuffer (*reader, input, request.source.startFrame + offset, count, channels);
        stretcher->study (input.getArrayOfReadPointers(), (size_t) count,
                          offset + count >= request.source.sourceFrames);
        offset += count;
    }

    juce::AudioBuffer<float> output (channels, (int) juce::jmin (blockFrames, targetFrames));
    int64_t written = 0, retrieved = 0;
    bool writeFailed = false;

    auto drain = [&]
    {
        for (;;)
        {
            // The R3 engine returns -1 once it is finished and its output ring is empty.
            const int available = stretcher->available();
            if (available <= 0) return true;
            if (cancel && cancel()) return false;
            const int count = juce::jmin (available, output.getNumSamples());
            const size_t got = stretcher->retrieve (output.getWritePointer (0) != nullptr
                                                        ? output.getArrayOfWritePointers() : nullptr,
                                                    (size_t) count);
            if (got == 0) return true;
            retrieved += (int64_t) got;
            if (! finiteAudio (output, (int) got))
            {
                result.error = "Matched audio contains NaN or infinity";
                return false;
            }
            const int64_t room = targetFrames - written;
            const int toWrite = (int) juce::jmin<int64_t> (room, (int64_t) got);
            if (toWrite > 0)
            {
                if (! writer->writeFromFloatArrays (output.getArrayOfReadPointers(), channels, toWrite))
                {
                    writeFailed = true;
                    return false;
                }
                written += toWrite;
            }
            if (got < (size_t) count) return true;
        }
    };

    for (int64_t offset = 0; offset < request.source.sourceFrames;)
    {
        if (cancel && cancel())
        {
            writer.reset();
            partial.deleteFile();
            result.cancelled = true;
            result.error = "Match cancelled";
            return result;
        }
        const int count = (int) juce::jmin<int64_t> (input.getNumSamples(), request.source.sourceFrames - offset);
        readIntoBuffer (*reader, input, request.source.startFrame + offset, count, channels);
        stretcher->process (input.getArrayOfReadPointers(), (size_t) count,
                            offset + count >= request.source.sourceFrames);
        offset += count;
        if (! drain())
        {
            writer.reset();
            partial.deleteFile();
            if (writeFailed) result.error = "Cannot write matched audio";
            else if (result.error.isEmpty())
            {
                result.cancelled = true;
                result.error = "Match cancelled";
            }
            return result;
        }
    }
    if (! drain())
    {
        writer.reset();
        partial.deleteFile();
        result.error = result.error.isNotEmpty() ? result.error : "Cannot finish matched audio";
        return result;
    }

    // Offline mode compensates for padding and delay, so the remaining error is tiny. A larger
    // mismatch is a failure, never a long silence or a chopped note.
    const int64_t tolerance = juce::jmax<int64_t> (1, (int64_t) std::llround (sampleRate / 1000.0));
    if (std::llabs (retrieved - targetFrames) > tolerance || std::llabs (written - targetFrames) > tolerance)
    {
        writer.reset();
        partial.deleteFile();
        result.error = "Matched length differs from the requested length by "
            + juce::String (retrieved - targetFrames) + " frames";
        return result;
    }
    if (written < targetFrames)
    {
        const int padding = (int) (targetFrames - written);
        output.clear();
        if (! writer->writeFromFloatArrays (output.getArrayOfReadPointers(), channels, padding))
        {
            writer.reset();
            partial.deleteFile();
            result.error = "Cannot pad the matched output";
            return result;
        }
        written = targetFrames;
    }
    if (! writer->flush())
    {
        writer.reset();
        partial.deleteFile();
        result.error = "Cannot finalise the matched WAV";
        return result;
    }
    writer.reset();

    // A source edited while the worker was running invalidates the whole result.
    const auto after = fingerprintOf (request.sourceFile);
    if (! sameFingerprint (before, after))
    {
        partial.deleteFile();
        result.error = "Source changed during Match preparation";
        return result;
    }
    juce::String validationError;
    if (! validateMatchOutput (partial, targetFrames, sampleRate, channels, validationError))
    {
        partial.deleteFile();
        result.error = "Matched WAV failed validation: " + validationError;
        return result;
    }
    if (! publishNoClobber (partial, request.outputFile))
    {
        // Another instance published the same key first. Its file is the result; ours is dropped.
        if (! request.outputFile.existsAsFile())
        {
            partial.deleteFile();
            result.error = "Cannot publish the matched WAV";
            return result;
        }
        partial.deleteFile();
        juce::String error;
        if (! validateMatchOutput (request.outputFile, targetFrames, sampleRate, channels, error))
        {
            result.error = "Published matched WAV is corrupt: " + error;
            return result;
        }
    }

    result.success = true;
    result.file = request.outputFile;
    result.frames = targetFrames;
    result.sampleRate = sampleRate;
    result.channels = channels;
    return result;
}
} // namespace sonicator
