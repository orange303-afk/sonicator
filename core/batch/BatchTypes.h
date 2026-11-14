#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <cstdint>
#include <variant>
#include <vector>

namespace sonicator::batch
{
// Batch Processing v1 contracts. The authoritative behaviour is documented in
// docs/superpowers/specs/2026-11-15-batch-processing-tz.md.
inline constexpr int maxSteps = 16;
inline constexpr int maxSources = 100000;
inline constexpr int64_t blockFrames = 16384;
// WAV and AIFF without RF64 cannot address a payload of 2^32 bytes or more.
inline constexpr int64_t containerByteLimit = 0x100000000LL;
inline constexpr int64_t spaceReserveBytes = 256LL * 1024 * 1024;

enum class Format { wav, aiff, flac };
enum class Encoding { sameAsSource, int16, int24, float32 };
enum class ChannelMode { sameAsSource, mono, stereo };
enum class DitherMode { automatic, tpdf, off };
enum class ConflictPolicy { rename, skip, replaceOutput };
enum class ClippingPolicy { rejectInteger, clipInteger };

struct RemoveDc
{
    bool operator==(const RemoveDc&) const = default;
};
struct TrimSilence
{
    double thresholdDb = -60.0; // -120..-6
    double paddingMs = 5.0;     // 0..1000
    bool operator==(const TrimSilence&) const = default;
};
struct Normalize
{
    double peakDb = -0.1; // -24..0
    bool operator==(const Normalize&) const = default;
};
struct Gain
{
    double gainDb = 0.0; // -60..+24
    bool operator==(const Gain&) const = default;
};
struct FadeIn
{
    double ms = 5.0; // 0..10000
    bool cosine = false;
    bool operator==(const FadeIn&) const = default;
};
struct FadeOut
{
    double ms = 5.0;
    bool cosine = false;
    bool operator==(const FadeOut&) const = default;
};
using StepParameters = std::variant<RemoveDc, TrimSilence, Normalize, Gain, FadeIn, FadeOut>;

struct ProcessStep
{
    bool enabled = true;
    StepParameters parameters { RemoveDc {} };
    bool operator==(const ProcessStep&) const = default;
};

struct OutputPreset
{
    Format format = Format::wav;
    Encoding encoding = Encoding::int24;
    int flacCompressionLevel = 5; // 1..8
    bool operator==(const OutputPreset&) const = default;
};

struct Recipe
{
    std::vector<ProcessStep> steps;
    std::vector<OutputPreset> outputs { OutputPreset {} };
    int sampleRate = 0; // 0 = same as source
    ChannelMode channels = ChannelMode::sameAsSource;
    DitherMode dither = DitherMode::automatic;
    ClippingPolicy clipping = ClippingPolicy::rejectInteger;
    // Explicit opt-in for saturating integer output; off by default (TZ section 7).
    bool allowIntegerClipping = false;
    bool operator==(const Recipe&) const = default;
};

struct SourceSelection
{
    enum class Kind { rows, folder };
    Kind kind = Kind::rows;
    std::vector<int64_t> rows;
    juce::String folder;
    // Ids the owner already knows to be edited and unsaved; they are reported, never exported.
    std::vector<int64_t> unsavedEditedRows;
};

struct Destination
{
    bool nextToSources = true;
    juce::File folder;
};

struct Request
{
    SourceSelection sources;
    Recipe recipe;
    Destination destination;
    ConflictPolicy conflicts = ConflictPolicy::rename;
    bool replacementConfirmed = false;
};

// Fatal codes and warning codes live in one enum; Issue::warning marks the latter.
enum class ErrorCode
{
    none,
    no_sources,
    too_many_sources,
    folder_not_registered,
    missing_source,
    unsupported_source,
    unsupported_channels,
    unsupported_sample_rate,
    decode_failed,
    empty_source,
    symlink_source_unsupported,
    unsaved_editor_changes,
    source_changed,
    destination_changed,
    replacement_not_confirmed,
    unsafe_destination,
    insufficient_space,
    clipping_prevented,
    encode_failed,
    validate_failed,
    publish_failed,
    cancelled,
    no_outputs,
    too_many_steps,
    invalid_recipe,
    invalid_encoding,
    container_limit,
    unknown_free_space,
    metadata_not_preserved,
    float_over_unity,
    integer_clipped,
    silent_trim_unchanged,
    conflict_skipped,
    stereo_partner_missing
};

juce::String errorCodeName(ErrorCode);
bool isWarningCode(ErrorCode);

struct Issue
{
    ErrorCode code = ErrorCode::none;
    juce::String message;
    bool warning = false;
    juce::String path;
};

// One accepted physical input. A stereo pair becomes two separate mono snapshots.
struct SourceSnapshot
{
    int64_t sampleId = 0;
    juce::File path;
    juce::String root;        // longest registered root, empty when none
    juce::String relativeDir; // folder of the source relative to root
    juce::String identity;    // dev:ino:size:mtime, used for alias protection
    int64_t fileSize = 0;
    int64_t modifiedMs = 0;
    int sampleRate = 0;
    int channels = 0;
    int bits = 0;
    bool floating = false;
    int64_t frames = 0;
    bool stereoPaired = false;
    juce::String pairedPartner;
};

struct OutputPlan
{
    size_t outputIndex = 0;
    size_t sourceIndex = 0;
    Format format = Format::wav;
    Encoding encoding = Encoding::int24;
    int bits = 24;
    bool floating = false;
    int sampleRate = 0;
    int channels = 0;
    juce::File target;
    bool replaceExisting = false;
    bool skipExisting = false;
    juce::String previousFingerprint;
    int64_t estimatedFrames = 0;
    int64_t estimatedBytes = 0;
};

struct PreparedBatch
{
    Request request;
    std::vector<SourceSnapshot> sources;
    std::vector<Issue> skipped;
    std::vector<OutputPlan> outputs;
    juce::String jobId;
    std::vector<juce::String> protectedIdentities;
    std::vector<juce::String> protectedPaths;
    int64_t estimatedBytes = 0;
    bool freeSpaceKnown = false;
    int64_t freeBytes = 0;
};

struct PreflightResult
{
    bool ok = false;
    PreparedBatch batch;
    std::vector<Issue> issues;
};

enum class OutputState { completed, skipped, failed, cancelled };

struct StepOutcome
{
    int stepIndex = 0;
    juce::String type;
    bool changed = false;
    juce::String detail;
    double value = 0.0;
};

struct OutputResult
{
    size_t outputIndex = 0;
    int64_t sampleId = 0;
    juce::File source;
    juce::File target;
    Format format = Format::wav;
    Encoding encoding = Encoding::int24;
    int bits = 24;
    bool floating = false;
    int sampleRate = 0;
    int channels = 0;
    int64_t frames = 0;
    float peak = 0.0f;
    OutputState state = OutputState::failed;
    ErrorCode code = ErrorCode::none;
    juce::String message;
    std::vector<Issue> warnings;
    std::vector<StepOutcome> steps;
    int64_t startedMs = 0;
    int64_t finishedMs = 0;
};

struct SourceResult
{
    int64_t sampleId = 0;
    juce::File path;
    int planned = 0;
    int completed = 0;
    int failed = 0;
    int skipped = 0;
    int cancelled = 0;
};

struct Progress
{
    bool running = false;
    bool cancelled = false;
    juce::String phase; // Preparing, Analyzing, Encoding, Validating, Publishing
    juce::String current;
    int outputsTotal = 0;
    int outputsDone = 0;
    double fraction = 0.0;
};

struct Report
{
    juce::String jobId;
    int64_t startedMs = 0;
    int64_t finishedMs = 0;
    juce::String recipeFingerprint;
    std::vector<OutputResult> outputs;
    std::vector<SourceResult> sources;
    std::vector<Issue> skippedSources;
    int totalSources = 0;
    int completed = 0;
    int skipped = 0;
    int failed = 0;
    int cancelled = 0;
    bool abandoned = false;
};

juce::String formatLabel(Format);
juce::String formatExtension(Format);
bool parseFormat(const juce::String&, Format&);
juce::String encodingLabel(Encoding);
juce::String channelModeLabel(ChannelMode);
juce::String stepTypeName(const StepParameters&);

// Static capability of a format/encoding combination, independent of the source.
bool encodingSupported(Format, Encoding, juce::String& reason);
// Resolves sameAsSource against the source representation. An unsupported result is an error;
// there is no silent fallback to another depth.
bool resolveEncoding(const OutputPreset&, const SourceSnapshot&, Encoding&, int& bits,
                     bool& floating, juce::String& reason);

// Steps are valid when values are inside the ranges from TZ section 6 and the count is at most
// maxSteps. Unsupported output presets are reported separately as issues.
std::vector<Issue> validateRecipe(const Recipe&);

// Stable identity of a recipe without source/destination/confirmation fields.
juce::String recipeFingerprint(const Recipe&);
juce::String sourceFingerprint(const SourceSnapshot&);

// Physical identity used to refuse aliasing: device, inode, size and modification time.
juce::String physicalIdentity(const juce::File&);

int64_t outputFrameEstimate(int64_t inputFrames, int inputRate, int outputRate);
int64_t outputByteEstimate(int64_t frames, int channels, int bits);
bool checkedFrameMath(int64_t frames, int inRate, int outRate, int64_t& out);
} // namespace sonicator::batch
