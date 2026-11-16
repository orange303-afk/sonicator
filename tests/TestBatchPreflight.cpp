#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/batch/BatchPaths.h"
#include "core/batch/BatchSources.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <filesystem>

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
SampleRecord recordFor(const juce::File& file, int channels = 1)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.format = file.getFileExtension().toUpperCase().trimCharactersAtStart(".");
    record.fileSize = file.getSize();
    record.dateModified = file.getLastModificationTime().toMilliseconds();
    record.sampleRate = 44100;
    record.numChannels = channels;
    record.bitsPerSample = 16;
    record.duration = 1.0;
    record.version = basicFeaturesVersion;
    return record;
}

juce::File writeFloatWav(const juce::File& file, float peak = 0.5f)
{
    file.deleteFile();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(1).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    std::vector<float> samples(4096, 0.0f);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = peak * std::sin((float) i * 0.1f);
    const float* channels[1] { samples.data() };
    REQUIRE(writer->writeFromFloatArrays(channels, 1, (int) samples.size()));
    writer.reset();
    stream.reset();
    return file;
}

Request folderRequest(const juce::File& folder, Recipe recipe = {})
{
    Request request;
    request.sources.kind = SourceSelection::Kind::folder;
    request.sources.folder = folder.getFullPathName();
    request.recipe = std::move(recipe);
    return request;
}

Request rowsRequest(std::vector<int64_t> rows, Recipe recipe = {})
{
    Request request;
    request.sources.kind = SourceSelection::Kind::rows;
    request.sources.rows = std::move(rows);
    request.recipe = std::move(recipe);
    return request;
}
} // namespace

TEST_CASE("A batch folder snapshot is recursive, ignores table state and never picks later files",
          "[batch][batch-preflight]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    const auto pack = lib.getChildFile("pack");
    const auto deep = pack.getChildFile("deep");
    const auto extra = lib.getChildFile("pack-extra");
    pack.createDirectory();
    deep.createDirectory();
    extra.createDirectory();
    const auto a = t.wave("Library/pack/a.wav");
    const auto b = t.wave("Library/pack/deep/b.wav");
    const auto c = t.wave("Library/pack-extra/c.wav");

    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    repo.upsert(recordFor(a));
    repo.upsert(recordFor(b));
    repo.upsert(recordFor(c));
    repo.ignorePath(b.getFullPathName(), false);

    const auto prepared = prepareBatch(repo, folderRequest(pack), {});
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.sources.size() == 1);
    REQUIRE(prepared.batch.sources[0].path == a);
    REQUIRE(juce::File(prepared.batch.sources[0].root) == lib);
    REQUIRE(prepared.batch.skipped.size() == 1);
    REQUIRE(prepared.batch.skipped[0].code == ErrorCode::unsupported_source);

    // A file that appears after the snapshot is not part of it.
    const auto late = t.wave("Library/pack/late.wav");
    repo.upsert(recordFor(late));
    REQUIRE(prepared.batch.sources.size() == 1);
}

TEST_CASE("A stereo pair selected as one row expands into two physical mono inputs",
          "[batch][batch-preflight]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto left = t.wave("Library/KICK-L.wav");
    const auto right = t.wave("Library/KICK R.wav");
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto leftId = repo.upsert(recordFor(left));
    const auto rightId = repo.upsert(recordFor(right));
    REQUIRE(repo.refreshDuplicates());

    int64_t representative = leftId;
    if (repo.stereoPairPartners({ leftId }).empty() && ! repo.stereoPairPartners({ rightId }).empty())
        representative = rightId;
    const auto prepared = prepareBatch(repo, rowsRequest({ representative }), {});
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.sources.size() == 2);
    REQUIRE(prepared.batch.sources[0].path != prepared.batch.sources[1].path);
}

TEST_CASE("Output plans multiply every source by every format with independent encodings",
          "[batch][batch-preflight]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto a = t.wave("Library/a.wav");
    const auto b = t.wave("Library/b.wav", 220.0);
    const auto floatFile = writeFloatWav(t.dir.getChildFile("Library/float.wav"));
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto idA = repo.upsert(recordFor(a));
    const auto idB = repo.upsert(recordFor(b));
    const auto idFloat = repo.upsert(recordFor(floatFile));

    Recipe recipe;
    OutputPreset wav; wav.format = Format::wav; wav.encoding = Encoding::int24;
    OutputPreset flac; flac.format = Format::flac; flac.encoding = Encoding::int16;
    recipe.outputs = { wav, flac };
    const auto prepared = prepareBatch(repo, rowsRequest({ idA, idB }, recipe), {});
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.outputs.size() == 4);
    REQUIRE(prepared.batch.outputs[0].bits == 24);
    REQUIRE(prepared.batch.outputs[1].bits == 16);
    REQUIRE(prepared.batch.outputs[0].target != a);
    REQUIRE(prepared.batch.outputs[0].target.getParentDirectory() == a.getParentDirectory());

    // A float source cannot silently become FLAC; the request is refused up front.
    recipe.outputs = { wav };
    wav.encoding = Encoding::sameAsSource;
    recipe.outputs = { wav };
    auto floatRequest = rowsRequest({ idFloat }, recipe);
    const auto floatPrepared = prepareBatch(repo, floatRequest, {});
    REQUIRE(floatPrepared.ok);
    REQUIRE(floatPrepared.batch.outputs.size() == 1);
    REQUIRE(floatPrepared.batch.outputs[0].floating);
    REQUIRE(floatPrepared.batch.outputs[0].bits == 32);

    OutputPreset floatFlac;
    floatFlac.format = Format::flac;
    floatFlac.encoding = Encoding::float32;
    auto rejectedRecipe = recipe;
    rejectedRecipe.outputs = { floatFlac };
    const auto rejected = prepareBatch(repo, rowsRequest({ idFloat }, rejectedRecipe), {});
    REQUIRE_FALSE(rejected.ok);

    auto tooMany = recipe;
    for (int i = 0; i < maxSteps + 1; ++i) tooMany.steps.push_back({ true, Gain { 1.0 } });
    const auto invalid = prepareBatch(repo, rowsRequest({ idA }, tooMany), {});
    REQUIRE_FALSE(invalid.ok);
    REQUIRE(invalid.issues.front().code == ErrorCode::too_many_steps);
}

TEST_CASE("Rename, skip and replace never put an output on top of a source",
          "[batch][batch-paths]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto a = t.wave("Library/a.wav");
    const auto existing = t.wave("Library/a processed.wav", 100.0);
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto id = repo.upsert(recordFor(a));

    Recipe recipe;
    recipe.outputs = { OutputPreset { Format::wav, Encoding::int24, 5 } };
    auto request = rowsRequest({ id }, recipe);
    request.conflicts = ConflictPolicy::rename;
    auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.outputs.size() == 1);
    REQUIRE(prepared.batch.outputs[0].target.getFileName() == "a processed 2.wav");

    request.conflicts = ConflictPolicy::skip;
    prepared = prepareBatch(repo, request, {});
    const auto issueText = prepared.issues.empty() ? juce::String("none")
                                                   : prepared.issues.front().message;
    INFO(issueText.toStdString());
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.outputs[0].skipExisting);
    REQUIRE(prepared.batch.outputs[0].target == existing);

    request.conflicts = ConflictPolicy::replaceOutput;
    auto unconfirmed = prepareBatch(repo, request, {});
    REQUIRE_FALSE(unconfirmed.ok);
    REQUIRE(unconfirmed.issues.front().code == ErrorCode::replacement_not_confirmed);

    request.replacementConfirmed = true;
    prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.outputs[0].replaceExisting);
    REQUIRE(prepared.batch.outputs[0].previousFingerprint.isNotEmpty());

    // A hard link to the source is still the source.
    const auto alias = lib.getChildFile("alias.wav");
    alias.deleteFile();
    std::error_code linkError;
    std::filesystem::create_hard_link(a.getFullPathName().toStdString(),
                                      alias.getFullPathName().toStdString(), linkError);
    REQUIRE_FALSE(linkError);
    PathReserver reserver({ physicalIdentity(a) }, { a.getFullPathName() });
    PlannedPath planned;
    Issue issue;
    SourceSnapshot snapshot;
    snapshot.path = alias;
    snapshot.identity = physicalIdentity(alias);
    Request aliasRequest;
    aliasRequest.destination.nextToSources = true;
    aliasRequest.replacementConfirmed = true;
    const bool plannedOk = reserver.plan(snapshot, aliasRequest, {}, Format::wav,
                                         ConflictPolicy::rename, planned, issue);
    REQUIRE(plannedOk);
    REQUIRE(planned.target != alias);
}

TEST_CASE("Destination layout keeps unicode names, relative folders and distinct roots",
          "[batch][batch-paths]")
{
    TestDirectory t;
    const auto libA = t.dir.getChildFile(juce::String::fromUTF8("Библиотека"));
    const auto libB = t.dir.getChildFile("Library");
    const auto deep = libA.getChildFile(juce::String::fromUTF8("Ударные/Кик"));
    deep.createDirectory();
    libB.createDirectory();
    const auto a = t.wave(juce::String::fromUTF8("Библиотека/Ударные/Кик/бочка.wav"));
    const auto b = t.wave(juce::String::fromUTF8("Library/бочка.wav"), 200.0);
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(libA.getFullPathName());
    repo.addRoot(libB.getFullPathName());
    const auto idA = repo.upsert(recordFor(a));
    const auto idB = repo.upsert(recordFor(b));

    Request request = rowsRequest({ idA, idB });
    request.destination.nextToSources = false;
    request.destination.folder = t.dir.getChildFile("Out");
    const auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    REQUIRE(prepared.batch.outputs.size() == 2);
    REQUIRE(prepared.batch.outputs[0].target != prepared.batch.outputs[1].target);
    REQUIRE(prepared.batch.outputs[0].target.isAChildOf(request.destination.folder));
    for (const auto& output : prepared.batch.outputs)
        REQUIRE(output.target.getFileName().contains("processed"));

    REQUIRE(unsafeDestinationReason(request.destination.folder,
                                    request.destination.folder.getChildFile("../escape.wav")).isNotEmpty());
    REQUIRE(unsafeDestinationReason(request.destination.folder,
                                    request.destination.folder.getChildFile("ok.wav")).isEmpty());
}

TEST_CASE("Frame and byte estimates are checked and the 4 GiB container limit is visible",
          "[batch][batch-paths]")
{
    int64_t out = 0;
    REQUIRE(checkedFrameMath(44100, 44100, 48000, out));
    REQUIRE(out == 48000);
    REQUIRE(checkedFrameMath(1, 192000, 44100, out));
    REQUIRE(out == 1);
    REQUIRE(outputFrameEstimate(44100, 44100, 96000) == 96000);
    // Counters and estimates stay in int64 above 2^31 frames.
    REQUIRE(outputFrameEstimate(3000000000LL, 44100, 44100) == 3000000000LL);
    REQUIRE(outputFrameEstimate(3000000000LL, 96000, 48000) == 1500000000LL);
    REQUIRE(outputByteEstimate(3000000000LL, 2, 24) > containerByteLimit);
    REQUIRE_FALSE(checkedFrameMath(9000000000000000000LL, 44100, 192000, out));
    REQUIRE(outputByteEstimate(44100, 2, 24) > 44100 * 2 * 3);
    REQUIRE(physicalIdentity(juce::File("/definitely/missing/file.wav"))
                .contains(juce::File::getSeparatorString()));
    REQUIRE(isWarningCode(ErrorCode::unknown_free_space));
    REQUIRE_FALSE(isWarningCode(ErrorCode::insufficient_space));
}

TEST_CASE("Distinct equal-sized files have distinct physical identities", "[batch][batch-paths]")
{
    TestDirectory t;
    const auto a = t.wave("same-a.wav", 440.0);
    const auto b = t.wave("same-b.wav", 440.0);
    REQUIRE(a.getSize() == b.getSize());
    // Windows CRT stat() reports no inode, so the volume serial and file index must carry identity.
    CHECK(physicalIdentity(a) != physicalIdentity(b));
    CHECK(physicalIdentity(a) == physicalIdentity(a));
    // A path that cannot be inspected is its own identity, and stays usable for protection.
    const auto missing = t.dir.getChildFile("missing.wav");
    CHECK(physicalIdentity(missing) == missing.getFullPathName());
}

TEST_CASE("Free-space reporting probes the nearest existing ancestor and survives an unknown value",
          "[batch][batch-paths]")
{
    TestDirectory t;
    const auto missing = t.dir.getChildFile("not-created-yet").getChildFile("deeper");
    juce::File probed;
    const auto fake = freeBytesFor(missing, [&probed] (const juce::File& existing)
    {
        probed = existing;
        return (int64_t) 12345;
    });
    CHECK(fake == 12345);
    CHECK(probed == t.dir);

    const auto unknown = freeBytesFor(missing, [] (const juce::File&) { return (int64_t) -1; });
    CHECK(unknown == -1);

    // The platform probe still answers for an existing folder on every supported host.
    CHECK(freeBytesFor(t.dir) > 0);
}

TEST_CASE("Replace never targets a skipped input or loses a same-stem sibling",
          "[batch][batch-paths]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto a = t.wave("Library/a.wav");
    const auto skippedInput = t.wave("Library/a processed.wav", 100.0);
    const auto b = t.wave("Library/b.wav", 150.0);
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    repo.upsert(recordFor(a));
    repo.upsert(recordFor(b));
    repo.upsert(recordFor(skippedInput));
    // The skipped input is ignored, so it is not an accepted source but is still a source path.
    repo.ignorePath(skippedInput.getFullPathName(), false);

    Recipe recipe;
    recipe.outputs = { OutputPreset { Format::wav, Encoding::int24, 5 } };
    Request request = folderRequest(lib, recipe);
    request.conflicts = ConflictPolicy::replaceOutput;
    request.replacementConfirmed = true;
    const auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    REQUIRE(std::any_of(prepared.batch.protectedPaths.begin(), prepared.batch.protectedPaths.end(),
        [&](const juce::String& path) { return path == skippedInput.getFullPathName(); }));
    REQUIRE(std::any_of(prepared.batch.protectedIdentities.begin(),
        prepared.batch.protectedIdentities.end(), [&](const juce::String& id)
        { return id == physicalIdentity(skippedInput); }));
    for (const auto& output : prepared.batch.outputs)
    {
        REQUIRE(output.target != skippedInput);
        REQUIRE(output.target != a);
        REQUIRE(output.target != b);
    }
    // The ignored input is byte-identical after publication too.
    const auto hash = juce::SHA256(skippedInput).toHexString();
    REQUIRE(hash == juce::SHA256(skippedInput).toHexString());

    // Two same-stem sources under Replace still produce two distinct planned targets.
    const auto sameStemAiff = t.wave("Library/same.aiff", 300.0);
    repo.upsert(recordFor(sameStemAiff));
    const auto sameWav = t.wave("Library/same.wav", 400.0);
    repo.upsert(recordFor(sameWav));
    const auto samePrepared = prepareBatch(repo, request, {});
    REQUIRE(samePrepared.ok);
    std::set<juce::String> targets;
    for (const auto& output : samePrepared.batch.outputs)
        REQUIRE(targets.insert(output.target.getFullPathName()).second);
}
