#include "BatchTypes.h"
#include <sys/stat.h>

namespace sonicator::batch
{
namespace
{
juce::String db(double value, int decimals = 4)
{
    return juce::String(value, decimals);
}
juce::String curveName(bool cosine) { return cosine ? "cosine" : "linear"; }

void appendStep(juce::String& out, const ProcessStep& step, int index)
{
    out << index << (step.enabled ? ":on:" : ":off:");
    if (auto* p = std::get_if<RemoveDc>(&step.parameters)) { out << "dc"; juce::ignoreUnused(p); }
    else if (auto* p = std::get_if<TrimSilence>(&step.parameters))
        out << "trim:" << db(p->thresholdDb) << ":" << db(p->paddingMs);
    else if (auto* p = std::get_if<Normalize>(&step.parameters))
        out << "normalize:" << db(p->peakDb);
    else if (auto* p = std::get_if<Gain>(&step.parameters))
        out << "gain:" << db(p->gainDb);
    else if (auto* p = std::get_if<FadeIn>(&step.parameters))
        out << "fadein:" << db(p->ms) << ":" << curveName(p->cosine);
    else if (auto* p = std::get_if<FadeOut>(&step.parameters))
        out << "fadeout:" << db(p->ms) << ":" << curveName(p->cosine);
    out << "\n";
}

bool inRange(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}
} // namespace

juce::String errorCodeName(ErrorCode code)
{
    switch (code)
    {
        case ErrorCode::none: return "none";
        case ErrorCode::no_sources: return "no_sources";
        case ErrorCode::too_many_sources: return "too_many_sources";
        case ErrorCode::folder_not_registered: return "folder_not_registered";
        case ErrorCode::missing_source: return "missing_source";
        case ErrorCode::unsupported_source: return "unsupported_source";
        case ErrorCode::unsupported_channels: return "unsupported_channels";
        case ErrorCode::unsupported_sample_rate: return "unsupported_sample_rate";
        case ErrorCode::decode_failed: return "decode_failed";
        case ErrorCode::empty_source: return "empty_source";
        case ErrorCode::symlink_source_unsupported: return "symlink_source_unsupported";
        case ErrorCode::unsaved_editor_changes: return "unsaved_editor_changes";
        case ErrorCode::source_changed: return "source_changed";
        case ErrorCode::destination_changed: return "destination_changed";
        case ErrorCode::replacement_not_confirmed: return "replacement_not_confirmed";
        case ErrorCode::unsafe_destination: return "unsafe_destination";
        case ErrorCode::insufficient_space: return "insufficient_space";
        case ErrorCode::clipping_prevented: return "clipping_prevented";
        case ErrorCode::encode_failed: return "encode_failed";
        case ErrorCode::validate_failed: return "validate_failed";
        case ErrorCode::publish_failed: return "publish_failed";
        case ErrorCode::cancelled: return "cancelled";
        case ErrorCode::no_outputs: return "no_outputs";
        case ErrorCode::too_many_steps: return "too_many_steps";
        case ErrorCode::invalid_recipe: return "invalid_recipe";
        case ErrorCode::invalid_encoding: return "invalid_encoding";
        case ErrorCode::container_limit: return "container_limit";
        case ErrorCode::unknown_free_space: return "unknown_free_space";
        case ErrorCode::metadata_not_preserved: return "metadata_not_preserved";
        case ErrorCode::float_over_unity: return "float_over_unity";
        case ErrorCode::integer_clipped: return "integer_clipped";
        case ErrorCode::silent_trim_unchanged: return "silent_trim_unchanged";
        case ErrorCode::conflict_skipped: return "conflict_skipped";
        case ErrorCode::stereo_partner_missing: return "stereo_partner_missing";
    }
    return "unknown";
}

bool isWarningCode(ErrorCode code)
{
    switch (code)
    {
        case ErrorCode::unknown_free_space:
        case ErrorCode::metadata_not_preserved:
        case ErrorCode::float_over_unity:
        case ErrorCode::integer_clipped:
        case ErrorCode::silent_trim_unchanged:
        case ErrorCode::conflict_skipped:
        case ErrorCode::stereo_partner_missing:
            return true;
        default:
            return false;
    }
}

juce::String formatLabel(Format format)
{
    switch (format)
    {
        case Format::wav: return "WAV";
        case Format::aiff: return "AIFF";
        case Format::flac: return "FLAC";
    }
    return {};
}

juce::String formatExtension(Format format)
{
    switch (format)
    {
        case Format::wav: return ".wav";
        case Format::aiff: return ".aif";
        case Format::flac: return ".flac";
    }
    return {};
}

bool parseFormat(const juce::String& text, Format& format)
{
    const auto lowered = text.toLowerCase();
    if (lowered == "wav") { format = Format::wav; return true; }
    if (lowered == "aiff" || lowered == "aif") { format = Format::aiff; return true; }
    if (lowered == "flac") { format = Format::flac; return true; }
    return false;
}

juce::String encodingLabel(Encoding encoding)
{
    switch (encoding)
    {
        case Encoding::sameAsSource: return "Same as source";
        case Encoding::int16: return "16-bit PCM";
        case Encoding::int24: return "24-bit PCM";
        case Encoding::float32: return "32-bit float";
    }
    return {};
}

juce::String channelModeLabel(ChannelMode mode)
{
    switch (mode)
    {
        case ChannelMode::sameAsSource: return "Same as source";
        case ChannelMode::mono: return "Mono";
        case ChannelMode::stereo: return "Stereo";
    }
    return {};
}

juce::String stepTypeName(const StepParameters& parameters)
{
    if (std::holds_alternative<RemoveDc>(parameters)) return "Remove DC offset";
    if (std::holds_alternative<TrimSilence>(parameters)) return "Trim silence";
    if (std::holds_alternative<Normalize>(parameters)) return "Normalize";
    if (std::holds_alternative<Gain>(parameters)) return "Gain";
    if (std::holds_alternative<FadeIn>(parameters)) return "Fade in";
    if (std::holds_alternative<FadeOut>(parameters)) return "Fade out";
    return "Step";
}

bool encodingSupported(Format format, Encoding encoding, juce::String& reason)
{
    switch (encoding)
    {
        case Encoding::sameAsSource:
            reason = "The source representation decides this depth";
            return true;
        case Encoding::int16:
        case Encoding::int24:
            return true;
        case Encoding::float32:
            if (format == Format::wav) return true;
            reason = format == Format::aiff
                ? "AIFF has no 32-bit float writer in this build; choose 16 or 24-bit"
                : "FLAC stores integer PCM only; choose 16 or 24-bit";
            return false;
    }
    return false;
}

bool resolveEncoding(const OutputPreset& preset, const SourceSnapshot& source, Encoding& encoding,
                     int& bits, bool& floating, juce::String& reason)
{
    reason.clear();
    encoding = preset.encoding;
    if (encoding == Encoding::sameAsSource)
    {
        if (source.floating)
        {
            if (preset.format != Format::wav)
            {
                reason = "This source is 32-bit float, but " + formatLabel(preset.format)
                    + " cannot store float; choose an explicit 16 or 24-bit depth";
                return false;
            }
            encoding = Encoding::float32;
        }
        else if (source.bits == 16 || source.bits == 24)
        {
            encoding = source.bits == 16 ? Encoding::int16 : Encoding::int24;
        }
        else
        {
            reason = "The source depth is not a supported PCM depth; choose an explicit 16 or 24-bit depth";
            return false;
        }
    }
    switch (encoding)
    {
        case Encoding::int16: bits = 16; floating = false; break;
        case Encoding::int24: bits = 24; floating = false; break;
        case Encoding::float32:
            if (preset.format != Format::wav)
            {
                reason = "Only WAV can store 32-bit float output";
                return false;
            }
            bits = 32; floating = true;
            break;
        case Encoding::sameAsSource:
            reason = "The source depth could not be resolved";
            return false;
    }
    if (preset.format == Format::flac && (preset.flacCompressionLevel < 1
                                          || preset.flacCompressionLevel > 8))
    {
        reason = "FLAC compression level must be 1..8";
        return false;
    }
    return true;
}

std::vector<Issue> validateRecipe(const Recipe& recipe)
{
    std::vector<Issue> issues;
    if ((int) recipe.steps.size() > maxSteps)
        issues.push_back({ ErrorCode::too_many_steps,
                           "At most " + juce::String(maxSteps) + " steps are allowed", false, {} });
    if (recipe.outputs.empty())
        issues.push_back({ ErrorCode::no_outputs, "Select at least one output format", false, {} });
    for (size_t i = 0; i < recipe.steps.size(); ++i)
    {
        const auto prefix = "Step " + juce::String((int) i + 1) + ": ";
        const auto& parameters = recipe.steps[i].parameters;
        juce::String problem;
        if (auto* p = std::get_if<TrimSilence>(&parameters))
        {
            if (! inRange(p->thresholdDb, -120.0, -6.0)) problem = "threshold must be -120..-6 dBFS";
            else if (! inRange(p->paddingMs, 0.0, 1000.0)) problem = "padding must be 0..1000 ms";
        }
        else if (auto* p = std::get_if<Normalize>(&parameters))
        {
            if (! inRange(p->peakDb, -24.0, 0.0)) problem = "peak target must be -24..0 dBFS";
        }
        else if (auto* p = std::get_if<Gain>(&parameters))
        {
            if (! inRange(p->gainDb, -60.0, 24.0)) problem = "gain must be -60..+24 dB";
        }
        else if (auto* p = std::get_if<FadeIn>(&parameters))
        {
            if (! inRange(p->ms, 0.0, 10000.0)) problem = "fade length must be 0..10000 ms";
        }
        else if (auto* p = std::get_if<FadeOut>(&parameters))
        {
            if (! inRange(p->ms, 0.0, 10000.0)) problem = "fade length must be 0..10000 ms";
        }
        if (problem.isNotEmpty())
            issues.push_back({ ErrorCode::invalid_recipe, prefix + problem, false, {} });
    }
    const bool rateKnown = recipe.sampleRate == 0 || recipe.sampleRate == 44100
        || recipe.sampleRate == 48000 || recipe.sampleRate == 88200
        || recipe.sampleRate == 96000 || recipe.sampleRate == 192000;
    if (! rateKnown)
        issues.push_back({ ErrorCode::invalid_recipe,
                           "The output sample rate is not one of the supported rates", false, {} });
    for (size_t i = 0; i < recipe.outputs.size(); ++i)
    {
        juce::String reason;
        if (! encodingSupported(recipe.outputs[i].format, recipe.outputs[i].encoding, reason))
            issues.push_back({ ErrorCode::invalid_encoding,
                               "Output " + juce::String((int) i + 1) + ": " + reason, false, {} });
    }
    return issues;
}

juce::String recipeFingerprint(const Recipe& recipe)
{
    juce::String text;
    text << "batch-recipe-1\n" << "rate:" << recipe.sampleRate << "\n"
         << "channels:" << (int) recipe.channels << "\n"
         << "dither:" << (int) recipe.dither << "\n"
         << "clipping:" << (int) recipe.clipping << ":" << (int) recipe.allowIntegerClipping << "\n";
    for (size_t i = 0; i < recipe.steps.size(); ++i) appendStep(text, recipe.steps[i], (int) i);
    for (const auto& output : recipe.outputs)
        text << "out:" << (int) output.format << ":" << (int) output.encoding << ":"
             << output.flacCompressionLevel << "\n";
    return juce::SHA256(text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString();
}

juce::String sourceFingerprint(const SourceSnapshot& source) { return source.identity; }

juce::String physicalIdentity(const juce::File& file)
{
    struct stat info {};
    if (stat(file.getFullPathName().toRawUTF8(), &info) != 0) return file.getFullPathName();
    return juce::String((juce::int64) info.st_dev) + ":" + juce::String((juce::int64) info.st_ino)
        + ":" + juce::String((juce::int64) info.st_size) + ":"
        + juce::String((juce::int64) info.st_mtime);
}

bool checkedFrameMath(int64_t frames, int inRate, int outRate, int64_t& out)
{
    if (frames <= 0 || inRate <= 0 || outRate <= 0) return false;
    const auto numerator = (double) frames * (double) outRate / (double) inRate;
    if (! std::isfinite(numerator) || numerator > 9.0e18) return false;
    out = std::max<int64_t>(1, (int64_t) std::llround(numerator));
    return true;
}

int64_t outputFrameEstimate(int64_t inputFrames, int inputRate, int outputRate)
{
    int64_t result = 0;
    if (! checkedFrameMath(std::max<int64_t>(inputFrames, 1), inputRate, outputRate, result)) return 0;
    return result;
}

int64_t outputByteEstimate(int64_t frames, int channels, int bits)
{
    const auto bytesPerSample = juce::jmax(2, bits / 8);
    if (frames <= 0 || channels <= 0) return 0;
    return frames * channels * bytesPerSample + 4096;
}
} // namespace sonicator::batch
