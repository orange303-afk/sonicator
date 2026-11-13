#include <catch2/catch_test_macros.hpp>
#include "core/indexing/CatalogFileSync.h"
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
juce::File writeWav(const juce::File& file, int frames)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(1).withBitsPerSample(16);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(1, frames);
    for (int i = 0; i < frames; ++i)
        buffer.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f
                                                * (float) i / 44100.0f));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}

int64_t addRow(SampleRepository& repo, const juce::File& file)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.format = "WAV";
    record.fileSize = file.getSize();
    record.dateModified = file.getLastModificationTime().toMilliseconds();
    return repo.upsert(record);
}

juce::StringArray tagNames(const std::vector<Tag>& tags)
{
    juce::StringArray names;
    for (const auto& tag : tags) names.add(tag.name);
    return names;
}
} // namespace

TEST_CASE("Refreshing a rewritten file keeps its tags, star and row id", "[edit][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto file = writeWav(t.dir.getChildFile("clap.wav"), 1000);
    const auto id = addRow(repo, file);
    const auto tagId = repo.createTag("Clap", 0);
    repo.setSampleTag(id, tagId, true);
    repo.setStarred({ id }, true);
    const std::vector<float> oldFeatures { 1.0f, 2.0f };
    repo.recordFeedback(id, "clap", { "Clap" }, oldFeatures, SampleRepository::FeedbackOrigin::user);
    const auto before = repo.feedbackForSample(id);
    REQUIRE(before.has_value());

    writeWav(file, 500);
    const auto result = refreshCatalogFile(repo, file);
    REQUIRE(result.ok);
    REQUIRE(result.sampleId == id);
    const auto record = repo.findByPath(file.getFullPathName());
    REQUIRE(record.has_value());
    REQUIRE(record->id == id);
    REQUIRE(tagNames(repo.tagsForSample(id)) == juce::StringArray { "Clap" });
    REQUIRE(repo.findById(id)->starred);
    const auto feedback = repo.feedbackForSample(id);
    REQUIRE(feedback.has_value());
    REQUIRE(feedback->features != oldFeatures);
    REQUIRE(feedback->updatedAt >= before->updatedAt);
    REQUIRE(feedback->fileHash == record->fileHash);
    REQUIRE(record->duration < 0.05);
}

TEST_CASE("Refreshing a file without feedback does not create a row", "[edit][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto file = writeWav(t.dir.getChildFile("plain.wav"), 1000);
    const auto id = addRow(repo, file);
    writeWav(file, 500);
    REQUIRE(refreshCatalogFile(repo, file).ok);
    REQUIRE_FALSE(repo.feedbackForSample(id).has_value());
}

TEST_CASE("Refreshing a file that is not in the catalog fails", "[edit][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto file = writeWav(t.dir.getChildFile("unknown.wav"), 1000);
    const auto result = refreshCatalogFile(repo, file);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("A catalog copy inherits tags and the star", "[edit][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto source = writeWav(t.dir.getChildFile("kick.wav"), 1000);
    const auto sourceId = addRow(repo, source);
    const auto tagId = repo.createTag("Kick", 0);
    repo.setSampleTag(sourceId, tagId, true);
    repo.setStarred({ sourceId }, true);
    const auto copy = writeWav(t.dir.getChildFile("kick edit.wav"), 500);
    const auto result = addCatalogCopy(repo, copy, sourceId);
    REQUIRE(result.ok);
    REQUIRE(result.sampleId > 0);
    REQUIRE(tagNames(repo.tagsForSample(result.sampleId)) == juce::StringArray { "Kick" });
    REQUIRE(repo.findById(result.sampleId)->starred);
    const auto record = repo.findByPath(copy.getFullPathName());
    REQUIRE(record.has_value());
    REQUIRE(record->id == result.sampleId);
}

TEST_CASE("A catalog copy of an unknown template still becomes a row", "[edit][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto copy = writeWav(t.dir.getChildFile("orphan edit.wav"), 500);
    const auto result = addCatalogCopy(repo, copy, 0);
    REQUIRE(result.ok);
    REQUIRE(repo.tagsForSample(result.sampleId).empty());
    REQUIRE_FALSE(repo.findById(result.sampleId)->starred);
}
