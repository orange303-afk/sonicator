#include <catch2/catch_test_macros.hpp>
#include "core/batch/BatchService.h"
#include "core/batch/BatchSources.h"
#include "core/audio/SonicatorFormats.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"

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

SampleRecord recordFor(const juce::File& file)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.format = "WAV";
    record.fileSize = file.getSize();
    record.dateModified = file.getLastModificationTime().toMilliseconds();
    record.sampleRate = 44100;
    record.numChannels = 1;
    record.bitsPerSample = 32;
    record.duration = 1.0;
    record.version = basicFeaturesVersion;
    return record;
}

juce::String fileHash(const juce::File& file)
{
    return juce::SHA256(file).toHexString();
}

int countTemporaries(const juce::File& folder)
{
    int count = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, true, "*.batchtmp",
                                                          juce::File::findFiles))
    {
        juce::ignoreUnused(entry);
        ++count;
    }
    return count;
}
} // namespace

TEST_CASE("A batch job renders every source and format and leaves the originals byte-identical",
          "[batch][batch-service]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto a = floatWav(lib.getChildFile("a.wav"), { 0.2f, -0.2f, 0.4f });
    const auto b = floatWav(lib.getChildFile("b.wav"), { 0.1f, 0.3f });
    const auto hashA = fileHash(a);
    const auto hashB = fileHash(b);
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto idA = repo.upsert(recordFor(a));
    const auto idB = repo.upsert(recordFor(b));

    Request request;
    request.sources.kind = SourceSelection::Kind::rows;
    request.sources.rows = { idA, idB };
    request.recipe.outputs = { OutputPreset { Format::wav, Encoding::int24, 5 },
                               OutputPreset { Format::flac, Encoding::int16, 5 } };
    auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.outputs.size() == 4);

    BatchService service;
    REQUIRE(service.start(prepared.batch));
    REQUIRE_FALSE(service.start(prepared.batch));
    REQUIRE(service.waitUntilFinished(60000));
    const auto report = service.results();
    REQUIRE(report.outputs.size() == 4);
    REQUIRE(report.completed == 4);
    REQUIRE(report.failed == 0);
    REQUIRE(report.sources.size() == 2);
    for (const auto& output : report.outputs)
    {
        REQUIRE(output.state == OutputState::completed);
        REQUIRE(output.target.existsAsFile());
    }
    REQUIRE(fileHash(a) == hashA);
    REQUIRE(fileHash(b) == hashB);
    REQUIRE(countTemporaries(lib) == 0);
    REQUIRE(BatchService::reportToJson(report).contains("recipeFingerprint"));
    REQUIRE(BatchService::reportToText(report).contains("completed"));
}

TEST_CASE("One failed output does not hide or delete the others", "[batch][batch-service]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto source = floatWav(lib.getChildFile("hot.wav"), { 0.9f, -0.9f });
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto id = repo.upsert(recordFor(source));

    Request request;
    request.sources.rows = { id };
    request.recipe.steps = { { true, Gain { 6.0 } } };
    request.recipe.outputs = { OutputPreset { Format::wav, Encoding::int16, 5 },
                               OutputPreset { Format::wav, Encoding::float32, 5 } };
    auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    BatchService service;
    REQUIRE(service.start(prepared.batch));
    REQUIRE(service.waitUntilFinished(60000));
    const auto report = service.results();
    REQUIRE(report.outputs.size() == 2);
    REQUIRE(report.failed == 1);
    REQUIRE(report.completed == 1);
    int rejected = 0, completed = 0;
    for (const auto& output : report.outputs)
    {
        if (output.state == OutputState::failed) { ++rejected; REQUIRE(output.code == ErrorCode::clipping_prevented); }
        if (output.state == OutputState::completed) { ++completed; REQUIRE(output.target.existsAsFile()); }
    }
    REQUIRE(rejected == 1);
    REQUIRE(completed == 1);
    REQUIRE(countTemporaries(lib) == 0);
}

TEST_CASE("Cancellation keeps every planned output in the report and cleans temporaries",
          "[batch][batch-service]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    std::vector<float> samples(200000);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = (float) std::sin((double) i * 0.01) * 0.3f;
    const auto source = floatWav(lib.getChildFile("long.wav"), samples);
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto id = repo.upsert(recordFor(source));
    Request request;
    request.sources.rows = { id };
    request.recipe.outputs = { OutputPreset { Format::wav, Encoding::int24, 5 },
                               OutputPreset { Format::flac, Encoding::int16, 5 },
                               OutputPreset { Format::aiff, Encoding::int24, 5 } };
    auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    BatchService service;
    REQUIRE(service.start(prepared.batch));
    service.cancel();
    REQUIRE(service.waitUntilFinished(60000));
    const auto report = service.results();
    REQUIRE(report.outputs.size() == 3);
    for (const auto& output : report.outputs)
    {
        REQUIRE((output.state == OutputState::completed || output.state == OutputState::cancelled
                 || output.state == OutputState::failed));
        if (output.state == OutputState::completed) REQUIRE(output.target.existsAsFile());
    }
    REQUIRE(countTemporaries(lib) == 0);
}

TEST_CASE("A source changed after the snapshot fails without touching the source",
          "[batch][batch-service]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto source = floatWav(lib.getChildFile("change.wav"), { 0.1f, 0.2f, 0.3f });
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto id = repo.upsert(recordFor(source));
    Request request;
    request.sources.rows = { id };
    request.recipe.outputs = { OutputPreset { Format::wav, Encoding::int16, 5 },
                               OutputPreset { Format::wav, Encoding::int24, 5 } };
    auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    source.appendText("modified");
    const auto changedHash = fileHash(source);
    BatchService service;
    REQUIRE(service.start(prepared.batch));
    REQUIRE(service.waitUntilFinished(60000));
    const auto report = service.results();
    REQUIRE(report.failed == 2);
    for (const auto& output : report.outputs)
        REQUIRE(output.code == ErrorCode::source_changed);
    REQUIRE(fileHash(source) == changedHash);
}

TEST_CASE("Diagnostic reports are JSON, bounded by count and age", "[batch][batch-service]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("reports");
    Report report;
    report.jobId = "job-1";
    report.totalSources = 1;
    const auto file = BatchService::saveReport(report, folder);
    REQUIRE(file.existsAsFile());
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    REQUIRE(parsed["jobId"].toString() == "job-1");

    for (int i = 0; i < 25; ++i)
    {
        Report more;
        more.jobId = "job-" + juce::String(i + 2);
        juce::Thread::sleep(2);
        BatchService::saveReport(more, folder);
    }
    int remaining = 0;
    juce::File oldest;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "batch-*.json",
                                                          juce::File::findFiles))
    {
        ++remaining;
        if (! oldest.existsAsFile() || entry.getFile().getLastModificationTime() < oldest.getLastModificationTime())
            oldest = entry.getFile();
    }
    REQUIRE(remaining == 20);

    oldest.setLastModificationTime(juce::Time(juce::Time::currentTimeMillis() - 40LL * 24 * 60 * 60 * 1000));
    Report newer;
    newer.jobId = "job-age";
    BatchService::saveReport(newer, folder, 20, 30);
    REQUIRE_FALSE(oldest.existsAsFile());
}
