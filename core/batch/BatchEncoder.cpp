#include "BatchEncoder.h"
#include "BatchMetadata.h"
#include "BatchQuantizer.h"
#include "core/audio/SonicatorFormats.h"
#include "core/export/AudioExporter.h"
#include <lame.h>
#include <cstring>
#include <vector>

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

std::vector<unsigned short> utf16z(const juce::String& text)
{
    std::vector<unsigned short> out;
    out.push_back(0xFEFFu); // LAME requires a UCS-2 byte-order marker.
    auto pointer = text.toUTF16();
    for (; ! pointer.isEmpty(); ++pointer) out.push_back((unsigned short) *pointer);
    out.push_back(0);
    return out;
}

void applyId3(lame_t lame, const TextTags& tags)
{
    id3tag_init(lame);
    id3tag_add_v2(lame);
    auto set = [lame](const char* frame, const juce::String& value)
    {
        if (value.isEmpty()) return;
        const auto text = utf16z(value);
        id3tag_set_textinfo_utf16(lame, frame, text.data());
    };
    set("TIT2", tags.title);
    set("TPE1", tags.artist);
    set("TALB", tags.album);
    set("TCON", tags.genre);
    set("TDRC", tags.date);
    set("TRCK", tags.track);
    if (tags.comment.isNotEmpty())
    {
        const auto text = utf16z(tags.comment);
        const auto empty = utf16z({});
        id3tag_set_comment_utf16(lame, "eng", empty.data(), text.data());
    }
}

// MP3 is lossy: LAME consumes float PCM itself, so no integer quantizer or dither runs. The
// encoder delay means the decoded length is compared within a codec tolerance, not exactly.
EncodedAudio encodeMp3ToTemporary(const juce::File& temporary, const EncodeSettings& settings,
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
    if (settings.mp3BitrateKbps < 64 || settings.mp3BitrateKbps > 320)
        return fail(ErrorCode::invalid_encoding, "MP3 bitrate must be 64..320 kbps");
    auto* lame = lame_init();
    if (lame == nullptr) return fail(ErrorCode::encode_failed, "The MP3 encoder could not start");
    lame_set_in_samplerate(lame, settings.sampleRate);
    lame_set_num_channels(lame, settings.channels);
    if (settings.mp3Mode == Mp3Mode::vbr)
    {
        lame_set_VBR(lame, vbr_default);
        lame_set_VBR_q(lame, juce::jlimit(0, 9, settings.mp3VbrQuality));
    }
    else
    {
        lame_set_brate(lame, settings.mp3BitrateKbps);
    }
    lame_set_quality(lame, 2);
    lame_set_bWriteVbrTag(lame, 1);
    applyId3(lame, settings.tags);
    if (lame_init_params(lame) < 0)
    {
        lame_close(lame);
        return fail(ErrorCode::invalid_encoding, "The MP3 encoder rejected the settings");
    }
    temporary.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> output(temporary.createOutputStream());
    if (output == nullptr)
    {
        lame_close(lame);
        return fail(ErrorCode::encode_failed, "The temporary file cannot be created");
    }
    // LAME emits the ID3v2 tag ahead of the first frame, so the placeholder Xing frame does not sit
    // at offset 0 and the finished tag must replace it where it actually is.
    std::vector<unsigned char> id3((size_t) ((double) blockFrames * 1.25) + 7200);
    const int id3Size = lame_get_id3v2_tag(lame, id3.data(), (int) id3.size());
    const int tagOffset = id3Size > 0 ? id3Size : 0;
    juce::AudioBuffer<float> block(settings.channels, (int) blockFrames);
    std::vector<float> left((size_t) blockFrames), right((size_t) blockFrames);
    std::vector<unsigned char> mp3((size_t) ((double) blockFrames * 1.25) + 7200);
    int64_t frames = 0;
    float peak = 0.0f;
    int64_t saturatedFrames = 0;
    bool writeOk = true;
    while (true)
    {
        if (cancelled(cancel)) { lame_close(lame); return fail(ErrorCode::cancelled, "Cancelled"); }
        const auto read = stream.readNext((int) blockFrames, block);
        if (read.state == ReadState::error || read.state == ReadState::cancelled)
        {
            lame_close(lame);
            return fail(read.code, read.error);
        }
        if (read.frames <= 0) break;
        for (int n = 0; n < read.frames; ++n)
        {
            bool frameSaturated = false;
            for (int channel = 0; channel < settings.channels; ++channel)
            {
                const auto value = block.getSample(channel, n);
                if (! std::isfinite(value))
                {
                    lame_close(lame);
                    return fail(ErrorCode::encode_failed,
                                "Processing produced NaN or infinite samples");
                }
                peak = std::max(peak, std::abs(value));
                if (std::abs(value) > 1.0f + 1.0e-7f) frameSaturated = true;
                if (channel == 0) left[(size_t) n] = value;
                else right[(size_t) n] = value;
            }
            if (frameSaturated) ++saturatedFrames;
        }
        const int encoded = lame_encode_buffer_ieee_float(
            lame, left.data(), settings.channels > 1 ? right.data() : nullptr, read.frames,
            mp3.data(), (int) mp3.size());
        if (encoded < 0)
        {
            lame_close(lame);
            return fail(ErrorCode::encode_failed, "The MP3 encoder failed");
        }
        if (encoded > 0 && ! output->write(mp3.data(), (size_t) encoded)) writeOk = false;
        frames += read.frames;
        if (! writeOk) { lame_close(lame); return fail(ErrorCode::encode_failed, "The MP3 file cannot be written"); }
    }
    const int tail = lame_encode_flush(lame, mp3.data(), (int) mp3.size());
    if (tail < 0 || (tail > 0 && ! output->write(mp3.data(), (size_t) tail)))
    {
        lame_close(lame);
        return fail(ErrorCode::encode_failed, "The MP3 file cannot be finalised");
    }
    // LAME writes a placeholder Xing/Info frame first and expects the caller to replace it with the
    // finished tag, as its own frontend does. Without that, a VBR file carries no frame count and
    // every reader — including this application's own — reports a duration guessed from the first
    // frame instead of the real one.
    const int tagSize = lame_get_lametag_frame(lame, mp3.data(), (int) mp3.size());
    if (tagSize > 0)
    {
        const auto end = output->getPosition();
        if (! output->setPosition(tagOffset) || ! output->write(mp3.data(), (size_t) tagSize)
            || ! output->setPosition(end))
        {
            lame_close(lame);
            return fail(ErrorCode::encode_failed, "The MP3 tag cannot be written");
        }
    }
    lame_close(lame);
    output.reset();
    if (frames != stream.frames())
        return fail(ErrorCode::encode_failed, "The encoded frame count is incomplete");
    if (phase) phase("Validating");
    float decodedPeak = 0.0f;
    for (const auto& issue : validateEncodedFile(temporary, settings, frames, decodedPeak, cancel))
        if (! issue.warning)
            return fail(issue.code == ErrorCode::none ? ErrorCode::validate_failed : issue.code,
                        issue.message);
    result.ok = true;
    result.frames = frames;
    result.peak = peak;
    result.saturatedFrames = saturatedFrames;
    result.floatOverUnity = false;
    return result;
}

EncodedAudio encodeOggToTemporary(const juce::File& temporary, const EncodeSettings& settings,
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
    if (settings.oggQuality < 0 || settings.oggQuality > 10)
        return fail(ErrorCode::invalid_encoding, "OGG quality must be 0..10");
    temporary.getParentDirectory().createDirectory();
    juce::OggVorbisAudioFormat format;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate((double) settings.sampleRate)
        .withNumChannels(settings.channels).withBitsPerSample(32)
        .withQualityOptionIndex(juce::jlimit(0, 10, settings.oggQuality));
    const std::pair<juce::String, juce::String> tags[] {
        { juce::OggVorbisAudioFormat::id3title, settings.tags.title },
        { juce::OggVorbisAudioFormat::id3artist, settings.tags.artist },
        { juce::OggVorbisAudioFormat::id3album, settings.tags.album },
        { juce::OggVorbisAudioFormat::id3date, settings.tags.date },
        { juce::OggVorbisAudioFormat::id3genre, settings.tags.genre },
        { juce::OggVorbisAudioFormat::id3trackNumber, settings.tags.track },
        { juce::OggVorbisAudioFormat::id3comment, settings.tags.comment },
    };
    for (const auto& tag : tags)
        if (tag.second.isNotEmpty()) options = options.withMetadata(tag.first, tag.second);
    std::unique_ptr<juce::OutputStream> output(temporary.createOutputStream());
    if (output == nullptr) return fail(ErrorCode::encode_failed, "The temporary file cannot be created");
    auto writer = format.createWriterFor(output, options);
    if (writer == nullptr) return fail(ErrorCode::invalid_encoding,
                                       "The OGG encoder rejected the settings");
    juce::AudioBuffer<float> block(settings.channels, (int) blockFrames);
    int64_t frames = 0;
    float peak = 0.0f;
    int64_t saturatedFrames = 0;
    while (true)
    {
        if (cancelled(cancel))
        {
            writer.reset();
            output.reset();
            return fail(ErrorCode::cancelled, "Cancelled");
        }
        const auto read = stream.readNext((int) blockFrames, block);
        if (read.state == ReadState::error || read.state == ReadState::cancelled)
        {
            writer.reset();
            output.reset();
            return fail(read.code, read.error);
        }
        if (read.frames <= 0) break;
        for (int n = 0; n < read.frames; ++n)
        {
            bool frameSaturated = false;
            for (int channel = 0; channel < settings.channels; ++channel)
            {
                const auto value = block.getSample(channel, n);
                if (! std::isfinite(value))
                {
                    writer.reset();
                    output.reset();
                    return fail(ErrorCode::encode_failed,
                                "Processing produced NaN or infinite samples");
                }
                peak = std::max(peak, std::abs(value));
                if (std::abs(value) > 1.0f + 1.0e-7f) frameSaturated = true;
            }
            if (frameSaturated) ++saturatedFrames;
        }
        if (! writer->writeFromFloatArrays(block.getArrayOfReadPointers(), settings.channels,
                                          read.frames))
        {
            writer.reset();
            output.reset();
            return fail(ErrorCode::encode_failed, "The OGG file cannot be written");
        }
        frames += read.frames;
    }
    writer.reset();
    output.reset();
    if (frames != stream.frames())
        return fail(ErrorCode::encode_failed, "The encoded frame count is incomplete");
    if (phase) phase("Validating");
    float decodedPeak = 0.0f;
    for (const auto& issue : validateEncodedFile(temporary, settings, frames, decodedPeak, cancel))
        if (! issue.warning)
            return fail(issue.code == ErrorCode::none ? ErrorCode::validate_failed : issue.code,
                        issue.message);
    result.ok = true;
    result.frames = frames;
    result.peak = peak;
    result.saturatedFrames = saturatedFrames;
    return result;
}
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
    if (settings.format == Format::mp3)
        return encodeMp3ToTemporary(temporary, settings, stream, cancel, phase);
    if (settings.format == Format::ogg)
        return encodeOggToTemporary(temporary, settings, stream, cancel, phase);
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
    if (settings.format == Format::mp3)
    {
        juce::AudioFormatManager formats;
        registerSonicatorFormats(formats);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            issues.push_back({ ErrorCode::validate_failed, "The MP3 file cannot be decoded", false, {} });
            return issues;
        }
        if ((int) std::lround(reader->sampleRate) != settings.sampleRate)
            issues.push_back({ ErrorCode::validate_failed, "The MP3 sample rate differs", false, {} });
        if ((int) reader->numChannels != settings.channels)
            issues.push_back({ ErrorCode::validate_failed, "The MP3 channel count differs", false, {} });
        const auto tolerance = (int64_t) (2 * 1152 + settings.sampleRate / 10);
        if (reader->lengthInSamples <= 0 || std::abs(reader->lengthInSamples - frames) > tolerance)
            issues.push_back({ ErrorCode::validate_failed,
                               "The decoded MP3 length " + juce::String((juce::int64) reader->lengthInSamples)
                                   + " is outside the codec tolerance for " + juce::String((juce::int64) frames),
                               false, {} });
        return issues;
    }
    if (settings.format == Format::ogg)
    {
        juce::AudioFormatManager formats;
        registerSonicatorFormats(formats);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            issues.push_back({ ErrorCode::validate_failed, "The OGG file cannot be decoded", false, {} });
            return issues;
        }
        if ((int) std::lround(reader->sampleRate) != settings.sampleRate)
            issues.push_back({ ErrorCode::validate_failed, "The OGG sample rate differs", false, {} });
        if ((int) reader->numChannels != settings.channels)
            issues.push_back({ ErrorCode::validate_failed, "The OGG channel count differs", false, {} });
        const auto tolerance = (int64_t) (4096 + settings.sampleRate / 10);
        if (reader->lengthInSamples <= 0 || std::abs(reader->lengthInSamples - frames) > tolerance)
            issues.push_back({ ErrorCode::validate_failed,
                               "The decoded OGG length is outside the codec tolerance", false, {} });
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
    settings.mp3Mode = plan.mp3Mode;
    settings.mp3BitrateKbps = plan.mp3BitrateKbps;
    settings.mp3VbrQuality = plan.mp3VbrQuality;
    settings.oggQuality = plan.oggQuality;
    settings.tags = metadata.tags;
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
