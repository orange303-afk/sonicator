#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "core/edit/AudioEditWriter.h"
#include "core/edit/NormalizeService.h"
#include "core/files/DuplicateCleanup.h"
#include "core/files/FileDisposal.h"
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"
#include <algorithm>
#include <cmath>

using namespace sonicator;

namespace
{
juce::File writeText(const juce::File& file, const juce::String& text)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText(text);
    return file;
}

juce::File writeWav(const juce::File& file, float amplitude, int frames = 2000)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(1).withBitsPerSample(16)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = wav.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    juce::AudioBuffer<float> buffer(1, frames);
    for (int i = 0; i < frames; ++i) buffer.setSample(0, i, amplitude);
    REQUIRE(writer->writeFromFloatArrays(buffer.getArrayOfReadPointers(), 1, frames));
    return file;
}

int countStagingFiles(const juce::File& folder)
{
    int count = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(
             folder, false, "*", juce::File::findFilesAndDirectories))
        if (entry.getFile().getFileName().startsWith(".")) ++count;
    return count;
}

juce::File writeJournal(const juce::File& file, const juce::File& staging, const juce::File& link,
                        const juce::File& original)
{
    juce::Array<juce::var> entries;
    auto* entry = new juce::DynamicObject();
    entry->setProperty("staging", staging.getFullPathName());
    entry->setProperty("link", link.getFullPathName());
    entry->setProperty("original", original.getFullPathName());
    entries.add(juce::var(entry));
    auto* root = new juce::DynamicObject();
    root->setProperty("entries", entries);
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(root), false));
    return file;
}
} // namespace

TEST_CASE("Disposal policy names round-trip through settings", "[files][settings]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("settings.json");
    AppSettings settings;
    settings.overwriteOriginalDisposal = files::DisposalPolicy::DeletePermanently;
    settings.alwaysOverwriteOriginalFiles = true;
    settings.ignoreSymlinks = true;
    SettingsStore(file).save(settings);
    const auto loaded = SettingsStore(file).load();
    REQUIRE(loaded.overwriteOriginalDisposal == files::DisposalPolicy::DeletePermanently);
    REQUIRE(loaded.alwaysOverwriteOriginalFiles);
    REQUIRE(loaded.ignoreSymlinks);

    AppSettings older;
    SettingsStore(file).save(older);
    const auto defaults = SettingsStore(file).load();
    REQUIRE(defaults.overwriteOriginalDisposal == files::DisposalPolicy::Trash);
    REQUIRE_FALSE(defaults.ignoreSymlinks);
    REQUIRE(files::disposalPolicyFromString("nonsense", files::DisposalPolicy::Trash)
            == files::DisposalPolicy::Trash);
}

TEST_CASE("Disposal removes a file and refuses links and missing paths", "[files][disposal]")
{
    TestDirectory t;
    const auto file = writeText(t.dir.getChildFile("take.wav"), "bytes");
    const auto outcome = files::disposeFile(file, files::DisposalPolicy::DeletePermanently);
    REQUIRE(outcome.ok);
    REQUIRE_FALSE(file.exists());

    REQUIRE_FALSE(files::disposeFile(t.dir.getChildFile("absent.wav"),
                                     files::DisposalPolicy::Trash).ok);

    const auto target = writeText(t.dir.getChildFile("target.wav"), "audio");
    const auto link = t.dir.getChildFile("alias.wav");
    // JUCE names the new link by the argument; the object it is called on is the target.
    if (! target.createSymbolicLink(link, false))
        SKIP("Symbolic links need Developer Mode or an elevated process on Windows");
    const auto refused = files::disposeFile(link, files::DisposalPolicy::DeletePermanently);
    REQUIRE_FALSE(refused.ok);
    REQUIRE(link.isSymbolicLink());
    REQUIRE(target.existsAsFile());

    // A headless CI container may have no desktop Trash; the product must report that, not delete.
    const auto trashed = writeText(t.dir.getChildFile("trash-me.wav"), "audio");
    const auto trashOutcome = files::disposeFile(trashed, files::DisposalPolicy::Trash);
    if (trashOutcome.ok) REQUIRE_FALSE(trashed.exists());
    else WARN("Trash is unavailable in this environment: " + trashOutcome.error);
}

TEST_CASE("Normalize permanently disposes of a replaced original without staging files",
          "[normalize][files]")
{
    TestDirectory t;
    const auto source = writeWav(t.dir.getChildFile("quiet.wav"), 0.1f);
    const auto before = source.getSize() > 0;
    REQUIRE(before);
    NormalizeOptions options;
    options.overwriteOriginals = true;
    options.originalDisposal = files::DisposalPolicy::DeletePermanently;
    const auto result = normalizeFile(source, options);
    REQUIRE(result.state == NormalizeFileState::normalized);
    REQUIRE(result.replacedOriginal);
    REQUIRE(result.destination == source);
    REQUIRE(source.existsAsFile());
    REQUIRE(result.warning.isEmpty());
    REQUIRE(countStagingFiles(t.dir) == 0);

    const auto peaks = [](const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr) return 0.0f;
        juce::AudioBuffer<float> buffer((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read(&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        return buffer.getMagnitude(0, buffer.getNumSamples());
    };
    REQUIRE(std::abs(peaks(source) - normalizePeakTarget) < 0.001f);
}

TEST_CASE("Duplicate cleanup moves only verified copies and keeps the original", "[files][duplicates]")
{
    TestDirectory t;
    const auto original = writeText(t.dir.getChildFile("keep/kick.wav"), "identical bytes");
    const auto copy = writeText(t.dir.getChildFile("drop/kick copy.wav"), "identical bytes");
    const auto different = writeText(t.dir.getChildFile("drop/other.wav"), "different bytes");

    juce::String error;
    REQUIRE(files::filesAreIdentical(original, copy, error));
    REQUIRE_FALSE(files::filesAreIdentical(original, different, error));

    files::DuplicateCleanupService service;
    // Permanent deletion: the test must not depend on a desktop Trash (headless CI has none).
    REQUIRE(service.start({ { original, copy, files::DuplicateAction::DeletePermanently, "h" },
                            { original, different, files::DuplicateAction::DeletePermanently, "h" } },
                          t.dir.getChildFile("journal.json")));
    REQUIRE(service.waitUntilFinished(10000));
    const auto results = service.results();
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].state == files::DuplicateCleanupState::removed);
    REQUIRE(results[1].state == files::DuplicateCleanupState::skipped);
    REQUIRE(original.existsAsFile());
    REQUIRE_FALSE(copy.exists());
    REQUIRE(different.existsAsFile());
    REQUIRE(service.progress().freedBytes == 15);
    REQUIRE(service.progress().trashedBytes == 0);
}

TEST_CASE("Trash cleanup reports bytes in Trash apart from freed space", "[files][duplicates]")
{
    TestDirectory t;
    const auto original = writeText(t.dir.getChildFile("keep/hat.wav"), "hat bytes");
    const auto copy = writeText(t.dir.getChildFile("drop/hat copy.wav"), "hat bytes");
    files::DuplicateCleanupService service;
    REQUIRE(service.start({ { original, copy, files::DuplicateAction::Trash, "h" } },
                          t.dir.getChildFile("journal.json")));
    REQUIRE(service.waitUntilFinished(10000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    // Without a desktop Trash the action must fail and keep the copy; do not pretend otherwise.
    if (results[0].state == files::DuplicateCleanupState::failed && copy.existsAsFile())
        SKIP("Trash is unavailable in this environment");
    REQUIRE(results[0].state == files::DuplicateCleanupState::removed);
    REQUIRE_FALSE(copy.exists());
    REQUIRE(service.progress().trashedBytes == original.getSize());
    REQUIRE(service.progress().freedBytes == 0);
}

TEST_CASE("Link replacement keeps the path usable and points at the original",
          "[files][duplicates]")
{
    TestDirectory t;
    const auto original = writeText(t.dir.getChildFile("keep/snare.wav"), "snare bytes");
    const auto copy = writeText(t.dir.getChildFile("drop/snare copy.wav"), "snare bytes");
    files::DuplicateCleanupService service;
    REQUIRE(service.start({ { original, copy, files::DuplicateAction::ReplaceWithLink, "h" } },
                          t.dir.getChildFile("journal.json")));
    REQUIRE(service.waitUntilFinished(10000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    if (results[0].state == files::DuplicateCleanupState::failed
        && results[0].message.containsIgnoreCase("symbolic link"))
        SKIP("Symbolic links need Developer Mode or an elevated process on Windows");
    REQUIRE(results[0].state == files::DuplicateCleanupState::linked);
    REQUIRE(copy.isSymbolicLink());
    // A link read back on Windows carries the NT namespace prefix, so the comparison normalises it
    // the same way the service does.
    const auto comparable = [](const juce::File& file)
    {
        auto path = file.getFullPathName();
        while (path.startsWith("\\\\?\\") || path.startsWith("\\??\\")) path = path.substring(4);
        path = path.replaceCharacter('\\', '/');
       #if JUCE_WINDOWS
        return path.toLowerCase();
       #endif
        return path;
    };
    REQUIRE(comparable(copy.getLinkedTarget()) == comparable(original));
    REQUIRE(copy.loadFileAsString() == "snare bytes");
    REQUIRE(original.loadFileAsString() == "snare bytes");
    REQUIRE(countStagingFiles(t.dir.getChildFile("drop")) == 0);
    REQUIRE_FALSE(t.dir.getChildFile("journal.json").existsAsFile());
    // The copy's bytes are gone; the content survives at the linked original.
    REQUIRE(service.progress().freedBytes == original.getSize());
    REQUIRE(service.progress().trashedBytes == 0);
}

TEST_CASE("An interrupted link replacement is restored from the journal", "[files][duplicates]")
{
    TestDirectory t;
    const auto original = writeText(t.dir.getChildFile("keep/loop.wav"), "loop bytes");
    const auto linkPath = t.dir.getChildFile("drop/loop copy.wav");
    const auto staging = t.dir.getChildFile("drop/.loop copy.wav.sonicator-cleanup-test");
    writeText(staging, "loop bytes");
    const auto journal = writeJournal(t.dir.getChildFile("journal.json"), staging, linkPath,
                                      original);
    const auto notes = files::DuplicateCleanupService::recoverInterrupted(journal);
    REQUIRE(notes.size() == 1);
    REQUIRE(linkPath.existsAsFile());
    REQUIRE_FALSE(linkPath.isSymbolicLink());
    REQUIRE(linkPath.loadFileAsString() == "loop bytes");
    REQUIRE_FALSE(staging.exists());
    REQUIRE_FALSE(journal.existsAsFile());
}

TEST_CASE("An interrupted replace-in-place restores the old file", "[files][edit]")
{
    TestDirectory t;
    const auto target = t.dir.getChildFile("song.wav");
    const auto staged = t.dir.getChildFile(".song.wav.sonicator-old-test");
    writeText(staged, "previous audio");
    const auto journal = t.dir.getChildFile("replace.json");
    auto* root = new juce::DynamicObject();
    root->setProperty("target", target.getFullPathName());
    root->setProperty("staged", staged.getFullPathName());
    root->setProperty("policy", juce::String("trash"));
    journal.replaceWithText(juce::JSON::toString(juce::var(root), false));

    const auto message = edit::recoverInterruptedReplacement(journal);
    REQUIRE(message.contains("Restored"));
    REQUIRE(target.existsAsFile());
    REQUIRE(target.loadFileAsString() == "previous audio");
    REQUIRE_FALSE(staged.exists());
    REQUIRE_FALSE(journal.existsAsFile());
}

TEST_CASE("The library can hide rows that are symbolic links", "[links][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("links.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto folder = t.dir.getChildFile("lib");
    folder.createDirectory();
    const auto real = writeText(folder.getChildFile("real.wav"), "audio bytes");
    const auto link = folder.getChildFile("alias.wav");
    // JUCE names the new link by the argument; the object it is called on is the target.
    if (! real.createSymbolicLink(link, false))
        SKIP("Symbolic links need Developer Mode or an elevated process on Windows");

    const auto add = [&repo](const juce::File& file)
    {
        SampleRecord record;
        record.filePath = file.getFullPathName();
        record.fileName = file.getFileName();
        record.fileHash = "hash";
        record.fileSize = 11;
        record.version = basicFeaturesVersion;
        record.numChannels = 1;
        record.sampleRate = 44100.0;
        record.duration = 1.0;
        return repo.upsert(record);
    };
    const auto realId = add(real);
    const auto linkId = add(link);

    // Older rows carry no flag until the one-time pass runs.
    REQUIRE_FALSE(repo.symlinkFlagsComplete());
    LibraryFilter filter;
    filter.ignoreSymlinks = true;
    REQUIRE(repo.queryIds(filter).size() == 2);

    REQUIRE(repo.refreshSymlinkFlags());
    REQUIRE(repo.symlinkFlagsComplete());
    const auto refreshed = repo.findByPath(link.getFullPathName());
    REQUIRE(refreshed.has_value());
    REQUIRE(refreshed->isLink);
    REQUIRE(repo.queryIds(filter) == std::vector<int64_t>{ realId });

    filter.ignoreSymlinks = false;
    REQUIRE(repo.queryIds(filter).size() == 2);
    repo.setSymlinkFlags({ link.getFullPathName() }, false);
    REQUIRE_FALSE(repo.findByPath(link.getFullPathName())->isLink);
    juce::ignoreUnused(linkId);
}

TEST_CASE("Duplicate groups shrink when a member row is removed", "[duplicates][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("dupes.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto folder = t.dir.getChildFile("lib").getFullPathName();
    const auto add = [&repo, &folder](const juce::String& name)
    {
        SampleRecord record;
        record.filePath = folder + "/" + name;
        record.fileName = name;
        record.fileHash = "same-bytes";
        record.fileSize = 42;
        record.version = basicFeaturesVersion;
        record.numChannels = 1;
        record.sampleRate = 44100.0;
        record.duration = 1.0;
        return repo.upsert(record);
    };
    add("a.wav");
    const auto b = add("b.wav");
    repo.refreshDuplicates();
    auto groups = repo.duplicateGroups();
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].members.size() == 2);

    repo.removeByPaths({ folder + "/b.wav" });
    repo.refreshDuplicateHashes({ "same-bytes" });
    REQUIRE(repo.duplicateGroups().empty());
    juce::ignoreUnused(b);
}
