#include "BatchEncoder.h"
#include "BatchMetadata.h"
#include "BatchQuantizer.h"
#include "core/audio/SonicatorFormats.h"
#include "core/export/AudioExporter.h"
#include <cstring>

namespace sonicator::batch
{
namespace
{
std::unique_ptr<juce::AudioFormat> formatFor(Format format)
{
    switch (format)
    {
        case Format::wav: return std::make_unique<juce::WavAudioFormat>();
        case Format::aiff: return std::make_unique<juce::AiffAudioFormat>();
        case Format::flac: return std::make_unique<juce::FlacAudioFormat>();
    }
    return {};
}

juce::String describeDouble(const juce::String& what, double expected, double actual)
{
    return what + " is " + juce::String(actual, 2) + " instead of " + juce::String(expected, 2);
}

bool cancelled(const CancelCheck& check) { return check && check(); }
} // namespace

EncodedAudio encodeToTemporary(const juce::File& temporary, const EncodeSettings& settings,
                               ConvertedStream& stream, CancelCheck cancel,
                               const std::function<void(const juce::String&)>& phase)
{
    EncodedAudio result;
    auto fail = [&temporary, &result](ErrorCode code, juce::String message)
    {
        temporary.deleteFile();
        result.ok = false;
        result.code = code;
        result.error = std::move(message);
        return result;
    };
    if (cancelled(cancel)) return fail(ErrorCode::cancelled, "Cancelled");
    if (temporary.existsAsFile())
        return fail(ErrorCode::encode_failed, "The temporary path already exists");
    if (settings.channels < 1 || settings.channels > 2 || settings.sampleRate <= 0)
        return fail(ErrorCode::invalid_encoding, "Unsupported output format settings");
    if (! settings.floating && settings.bits != 16 && settings.bits != 24)
        return fail(ErrorCode::invalid_encoding, "Unsupported integer depth");
    auto format = formatFor(settings.format);
    if (format == nullptr) return fail(ErrorCode::invalid_encoding, "No writer for this format");
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate((double) settings.sampleRate)
        .withNumChannels(settings.channels)
        .withBitsPerSample(settings.floating ? 32 : settings.bits)
        .withSampleFormat(settings.floating
                              ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                              : juce::AudioFormatWriterOptions::SampleFormat::integral);
    if (settings.format == Format::flac)
        options = options.withQualityOptionIndex(juce::jlimit(1, 8, settings.flacCompressionLevel));
    for (int i = 0; i < settings.metadata.size(); ++i)
        options = options.withMetadata(settings.metadata.getAllKeys()[i],
                                       settings.metadata.getAllValues()[i]);

    temporary.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> output(temporary.createOutputStream());
    if (output == nullptr) return fail(ErrorCode::encode_failed, "The temporary file cannot be created");
    auto writer = format->createWriterFor(output, options);
    if (writer == nullptr)
        return fail(ErrorCode::encode_failed, "The format rejected the audio settings");

    juce::AudioBuffer<float> block(settings.channels, (int) blockFrames);
    Quantizer quantizer;
    quantizer.reset(settings.ditherSeed, settings.dither && ! settings.floating,
                    settings.floating ? 32 : settings.bits);
    std::vector<int32_t> integers((size_t) settings.channels * (size_t) blockFrames);
    std::vector<int32_t*> intPointers((size_t) settings.channels);
    for (int channel = 0; channel < settings.channels; ++channel)
        intPointers[(size_t) channel] = integers.data() + (size_t) channel * (size_t) blockFrames;

    int64_t frames = 0;
    float peak = 0.0f;
    int64_t saturatedFrames = 0;
    while (true)
    {
        if (cancelled(cancel)) return fail(ErrorCode::cancelled, "Cancelled");
        auto read = stream.readNext((int) blockFrames, block);
        if (read.state == ReadState::error || read.state == ReadState::cancelled)
            return fail(read.code, read.error);
        if (read.frames <= 0) break;
        for (int n = 0; n < read.frames; ++n)
        {
            bool frameSaturated = false;
            for (int channel = 0; channel < settings.channels; ++channel)
            {
                const auto value = block.getSample(channel, n);
                if (! std::isfinite(value))
                    return fail(ErrorCode::encode_failed,
                                "Processing produced NaN or infinite samples");
                peak = std::max(peak, std::abs(value));
                if (std::abs(value) > 1.0f + 1.0e-7f) frameSaturated = true;
            }
            if (frameSaturated) ++saturatedFrames;
        }
        const bool wrote = settings.floating
            ? writer->writeFromFloatArrays(block.getArrayOfReadPointers(), settings.channels,
                                           read.frames)
            : (quantizer.quantize(block.getArrayOfReadPointers(), settings.channels, read.frames,
                                  intPointers.data()),
               writer->write((const int**) intPointers.data(), read.frames));
        if (! wrote) return fail(ErrorCode::encode_failed, "The audio could not be written");
        frames += read.frames;
    }
    const bool needsFlush = settings.format == Format::wav;
    if (needsFlush && ! writer->flush())
    {
        writer.reset();
        output.reset();
        return fail(ErrorCode::encode_failed, "The file could not be finalised");
    }
    writer.reset();
    output.reset();
    if (frames != stream.frames())
        return fail(ErrorCode::encode_failed, "The encoded frame count is incomplete");

    if (phase) phase("Validating");
    float decodedPeak = 0.0f;
    const auto validation = validateEncodedFile(temporary, settings, frames, decodedPeak, cancel);
    for (const auto& issue : validation)
        if (! issue.warning)
            return fail(issue.code == ErrorCode::none ? ErrorCode::validate_failed : issue.code,
                        issue.message);
    result.ok = true;
    result.frames = frames;
    result.peak = peak;
    result.saturatedFrames = saturatedFrames;
    result.floatOverUnity = settings.floating && peak > 1.0f + 1.0e-7f;
    return result;
}

std::vector<Issue> validateEncodedFile(const juce::File& file, const EncodeSettings& settings,
                                       int64_t frames, float& decodedPeak, CancelCheck cancel)
{
    std::vector<Issue> issues;
    decodedPeak = 0.0f;
    if (! file.existsAsFile())
    {
        issues.push_back({ ErrorCode::validate_failed, "The written file does not exist", false, {} });
        return issues;
    }
    if (settings.format == Format::wav)
    {
        const auto error = AudioExporter::validateWavOutput(file, frames,
            (double) settings.sampleRate, settings.channels, settings.floating ? 32 : settings.bits,
            settings.floating);
        if (error.isNotEmpty())
            issues.push_back({ ErrorCode::validate_failed, error, false, {} });
    }
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        issues.push_back({ ErrorCode::validate_failed, "The written file cannot be decoded", false, {} });
        return issues;
    }
    if (reader->lengthInSamples != frames)
        issues.push_back({ ErrorCode::validate_failed,
                           describeDouble("The written length", (double) frames,
                                          (double) reader->lengthInSamples), false, {} });
    if ((int) std::lround(reader->sampleRate) != settings.sampleRate)
        issues.push_back({ ErrorCode::validate_failed,
                           describeDouble("The written sample rate", settings.sampleRate,
                                          reader->sampleRate), false, {} });
    if ((int) reader->numChannels != settings.channels)
        issues.push_back({ ErrorCode::validate_failed,
                           "The written channel count is " + juce::String((int) reader->numChannels),
                           false, {} });
    if (! settings.floating)
    {
        if ((int) reader->bitsPerSample != settings.bits || reader->usesFloatingPointData)
            issues.push_back({ ErrorCode::validate_failed,
                               "The written bit depth or sample format differs", false, {} });
    }
    else if (! reader->usesFloatingPointData)
    {
        issues.push_back({ ErrorCode::validate_failed,
                           "The written file is not floating point", false, {} });
    }
    juce::AudioBuffer<float> block(std::max(1, (int) reader->numChannels), 65536);
    int64_t position = 0;
    while (position < reader->lengthInSamples)
    {
        if (cancelled(cancel))
        {
            issues.push_back({ ErrorCode::cancelled, "Cancelled", false, {} });
            return issues;
        }
        const int wanted = (int) std::min<int64_t>(65536, reader->lengthInSamples - position);
        if (! reader->read(&block, 0, wanted, position, true, true))
        {
            issues.push_back({ ErrorCode::validate_failed,
                               "The written file could not be decoded to the end", false, {} });
            return issues;
        }
        for (int channel = 0; channel < (int) reader->numChannels; ++channel)
            for (int n = 0; n < wanted; ++n)
            {
                const auto value = block.getSample(channel, n);
                if (! std::isfinite(value))
                {
                    issues.push_back({ ErrorCode::validate_failed,
                                       "The written file contains NaN or infinite samples", false, {} });
                    return issues;
                }
                decodedPeak = std::max(decodedPeak, std::abs(value));
            }
        position += wanted;
    }
    return issues;
}

OutputResult renderAndPublish(std::unique_ptr<PreparedAudio> prepared, const OutputPlan& plan,
                              const SourceSnapshot& source, const Recipe& recipe,
                              ConflictPolicy policy, const InputProtectionSet& protection,
                              CancelCheck cancel,
                              const std::function<void(const juce::String&)>& phase)
{
    OutputResult result;
    result.outputIndex = plan.outputIndex;
    result.sampleId = source.sampleId;
    result.source = source.path;
    result.target = plan.target;
    result.format = plan.format;
    result.encoding = plan.encoding;
    result.bits = plan.bits;
    result.floating = plan.floating;
    result.sampleRate = plan.sampleRate;
    result.channels = plan.channels;
    result.state = OutputState::failed;
    result.code = ErrorCode::encode_failed;
    result.startedMs = juce::Time::currentTimeMillis();
    auto finish = [&result](OutputState state, ErrorCode code, juce::String message)
    {
        result.state = state;
        result.code = code;
        result.message = std::move(message);
        result.finishedMs = juce::Time::currentTimeMillis();
        return result;
    };
    if (prepared == nullptr) return finish(OutputState::failed, ErrorCode::encode_failed,
                                           "No prepared audio");
    if (phase) phase("Analyzing");
    result.steps = prepared->stepOutcomes();

    bool amplitudeChanged = plan.sampleRate != prepared->sampleRate()
        || plan.channels != prepared->channels();
    for (const auto& outcome : prepared->stepOutcomes())
        if (outcome.changed) amplitudeChanged = true;

    bool timeChanged = plan.sampleRate != prepared->sampleRate();
    for (const auto& step : recipe.steps)
        if (step.enabled && std::holds_alternative<TrimSilence>(step.parameters)) timeChanged = true;

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> sourceReader(formats.createReaderFor(source.path));
    if (sourceReader == nullptr)
        return finish(OutputState::failed, ErrorCode::decode_failed,
                      "The source metadata could not be read");
    auto metadata = prepareMetadata(*sourceReader, plan.format, timeChanged);
    result.warnings.insert(result.warnings.end(), metadata.warnings.begin(), metadata.warnings.end());

    EncodeSettings settings;
    settings.format = plan.format;
    settings.bits = plan.bits;
    settings.floating = plan.floating;
    settings.sampleRate = plan.sampleRate;
    settings.channels = plan.channels;
    settings.flacCompressionLevel = recipe.outputs.empty()
        ? 5
        : [&recipe, &plan]
        {
            for (const auto& output : recipe.outputs)
                if (output.format == plan.format) return output.flacCompressionLevel;
            return 5;
        }();
    settings.dither = recipe.dither == DitherMode::tpdf
        || (recipe.dither == DitherMode::automatic
            && automaticDither(plan.encoding, source, amplitudeChanged));
    settings.ditherSeed = ditherSeed(source, plan.format, plan.encoding);
    settings.metadata = metadata.values;

    if (phase) phase("Encoding");
    auto converted = std::make_unique<ConvertedStream>(std::move(prepared), plan.sampleRate,
                                                       plan.channels, cancel);
    const auto temporary = createBatchTemporary(plan.target);
    struct TemporaryGuard
    {
        juce::File file;
        ~TemporaryGuard()
        {
            if (file.existsAsFile()) file.deleteFile();
        }
    } guard { temporary };
    auto encoded = encodeToTemporary(temporary, settings, *converted, cancel, phase);
    if (! encoded.ok)
    {
        temporary.deleteFile();
        return finish(OutputState::failed, encoded.code, encoded.error);
    }
    result.frames = encoded.frames;
    result.peak = encoded.peak;

    if (encoded.floatOverUnity)
        result.warnings.push_back({ ErrorCode::float_over_unity,
                                    "The float output contains samples above 0 dBFS", true, {} });
    if (! plan.floating && encoded.peak > 1.0f + 1.0e-7f)
    {
        if (! recipe.allowIntegerClipping)
        {
            temporary.deleteFile();
            return finish(OutputState::failed, ErrorCode::clipping_prevented,
                          "The peak " + juce::String(encoded.peak, 4)
                              + " would clip integer output; lower Gain or the Normalize target");
        }
        result.warnings.push_back({ ErrorCode::integer_clipped,
                                    "Integer clipping saturated " + juce::String(encoded.saturatedFrames)
                                        + " frame(s)",
                                    true, {} });
    }

    if (phase) phase("Publishing");
    const auto published = publishBatchFile(temporary, plan, policy, protection, source, cancel);
    if (! published.ok)
    {
        temporary.deleteFile();
        return finish(OutputState::failed, published.code, published.error);
    }
    result.target = published.finalPath;
    return finish(OutputState::completed, ErrorCode::none, {});
}
} // namespace sonicator::batch
