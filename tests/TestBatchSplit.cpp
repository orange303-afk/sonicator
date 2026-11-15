#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/batch/BatchPaths.h"
#include "core/batch/BatchService.h"
#include "core/batch/BatchSplit.h"
#include "core/batch/BatchSources.h"
#include "core/database/SampleRepository.h"
#include "core/audio/SonicatorFormats.h"
#include "TestHelpers.h"
#include <set>

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
juce::File floatWav(const juce::File& file, const std::vector<float>& samples)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(1)
        .withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    const float* channels[1] { samples.data() };
    REQUIRE(writer->writeFromFloatArrays(channels, 1, (int) samples.size()));
    writer.reset();
    stream.reset();
    return file;
}

SourceSnapshot snapshotFor(const juce::File& file)
{
    SourceSnapshot snapshot;
    snapshot.path = file;
    snapshot.identity = physicalIdentity(file);
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    snapshot.sampleRate = (int) reader->sampleRate;
    snapshot.channels = (int) reader->numChannels;
    snapshot.bits = (int) reader->bitsPerSample;
    snapshot.floating = reader->usesFloatingPointData;
    snapshot.frames = reader->lengthInSamples;
    return snapshot;
}

std::vector<float> twoPartTone(float first = 0.2f, float second = 0.5f)
{
    constexpr int rate = 44100;
    std::vector<float> samples((size_t) rate * 2, 0.0f);
    for (int i = 0; i < rate / 4; ++i)
        samples[(size_t) i] = first * (float) std::sin((double) i * 0.1);
    for (int i = 0; i < rate / 4; ++i)
        samples[(size_t) (rate * 5 / 4 + i)] = second * (float) std::sin((double) i * 0.1);
    return samples;
}

SampleRecord recordFor(const juce::File& file)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.format = "WAV";
    record.sampleRate = 44100;
    record.numChannels = 1;
    record.bitsPerSample = 32;
    record.duration = 2.0;
    record.version = basicFeaturesVersion;
    return record;
}

SplitSilence splitOptions()
{
    SplitSilence split;
    split.thresholdDb = -50.0;
    split.minSilenceMs = 250.0;
    split.minPartMs = 100.0;
    return split;
}
} // namespace

TEST_CASE("Split detection finds parts on the prepared pre-split stream", "[batch][batch-split]")
{
    TestDirectory t;
    const auto file = floatWav(t.dir.getChildFile("Source/parts.wav"), twoPartTone());
    Recipe recipe;
    recipe.steps = { { true, splitOptions() } };
    auto detectionAudio = prepareAudio(snapshotFor(file), recipe, {}, {}, true);
    REQUIRE(detectionAudio.ok);
    const auto detection = detectBatchParts(*detectionAudio.audio, splitOptions());
    REQUIRE(detection.split);
    REQUIRE(detection.parts.size() == 2);
    REQUIRE(detection.parts[0].begin <= 64);
    REQUIRE(detection.parts[0].end <= 44100 / 4 + 64);
    REQUIRE(detection.parts[1].begin >= 44100 * 5 / 4 - 64);
    REQUIRE(detection.parts[1].end <= 44100 * 3 / 2 + 64);

    const auto silent = floatWav(t.dir.getChildFile("Source/silent.wav"),
                                 std::vector<float>(44100, 0.0f));
    auto silentAudio = prepareAudio(snapshotFor(silent), recipe, {}, {}, true);
    REQUIRE(silentAudio.ok);
    const auto silentDetection = detectBatchParts(*silentAudio.audio, splitOptions());
    REQUIRE_FALSE(silentDetection.split);
    REQUIRE(silentDetection.parts.size() == 1);
    REQUIRE(silentDetection.parts[0].end == silentAudio.audio->frames());

    SplitSilence shortParts = splitOptions();
    shortParts.minPartMs = 1000.0;
    const auto dropped = detectBatchParts(*detectionAudio.audio, shortParts);
    REQUIRE_FALSE(dropped.split);
    REQUIRE(dropped.discardedShortParts >= 1);
}

TEST_CASE("Steps after Split measure only their part", "[batch][batch-split][batch-pipeline]")
{
    TestDirectory t;
    const auto file = floatWav(t.dir.getChildFile("Source/levels.wav"), twoPartTone(0.1f, 0.5f));
    const auto snapshot = snapshotFor(file);
    Recipe recipe;
    recipe.steps = { { true, splitOptions() }, { true, Normalize { -6.0 } } };
    auto detectionAudio = prepareAudio(snapshot, recipe, {}, {}, true);
    REQUIRE(detectionAudio.ok);
    const auto detection = detectBatchParts(*detectionAudio.audio, splitOptions());
    REQUIRE(detection.parts.size() == 2);

    auto part = prepareAudio(snapshot, recipe, {},
                             FrameRange { true, detection.parts[0].begin, detection.parts[0].end });
    REQUIRE(part.ok);
    juce::AudioBuffer<float> block(1, 4096);
    float peak = 0.0f;
    // If whole-file normalization had been used, part 1 would stay near 0.1.
    int64_t position = 0;
    while (position < part.audio->frames())
    {
        const auto read = part.audio->readFrames(position,
            (int) std::min<int64_t>(4096, part.audio->frames() - position), block);
        REQUIRE(read.state == ReadState::ok);
        for (int n = 0; n < read.frames; ++n) peak = std::max(peak, std::abs(block.getSample(0, n)));
        position += read.frames;
    }
    REQUIRE(peak == Catch::Approx(0.501187f).margin(0.01f));
}

TEST_CASE("A split batch writes numbered parts and isolates per-part failures",
          "[batch][batch-split][batch-service]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto file = floatWav(lib.getChildFile("two.wav"), twoPartTone(0.5f, 0.9f));
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto id = repo.upsert(recordFor(file));

    Request request;
    request.sources.rows = { id };
    request.recipe.steps = { { true, splitOptions() }, { true, Gain { 6.0 } } };
    request.recipe.outputs = { OutputPreset { Format::wav, Encoding::int16, 5 },
                               OutputPreset { Format::flac, Encoding::int16, 5 } };
    const auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    auto detectAudio = prepareAudio(snapshotFor(file), request.recipe, {}, {}, true);
    REQUIRE(detectAudio.ok);
    const auto detected = detectBatchParts(*detectAudio.audio, *recipeSplit(request.recipe));
    REQUIRE(detected.parts.size() == 2);

    BatchService service;
    REQUIRE(service.start(prepared.batch));
    REQUIRE(service.waitUntilFinished(120000));
    const auto report = service.results();
    REQUIRE(report.outputs.size() == 4);
    int completed = 0, failed = 0;
    std::set<juce::String> paths;
    for (const auto& output : report.outputs)
    {
        REQUIRE(output.partCount == 2);
        REQUIRE((output.partIndex == 1 || output.partIndex == 2));
        if (output.state == OutputState::completed)
        {
            ++completed;
            REQUIRE(output.target.getFileName().contains("part 00"));
            paths.insert(output.target.getFullPathName());
        }
        else if (output.state == OutputState::failed)
        {
            ++failed;
            REQUIRE(output.code == ErrorCode::clipping_prevented);
        }
    }
    REQUIRE(completed == 2); // both formats of the quiet part
    REQUIRE(failed == 2);    // both formats of the loud part
    REQUIRE(paths.size() == 2);

    request.conflicts = ConflictPolicy::replaceOutput;
    request.replacementConfirmed = true;
    const auto refused = prepareBatch(repo, request, {});
    REQUIRE_FALSE(refused.ok);
    REQUIRE(refused.issues.front().message.contains("Split"));
}
