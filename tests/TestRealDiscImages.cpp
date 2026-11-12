#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/disc/AkaiFilesystem.h"
#include "core/disc/AkaiSampleReader.h"
#include "core/disc/CueSheet.h"
#include "core/disc/DiscContentIndexer.h"
#include "core/disc/DiscFilesystem.h"
#include "core/disc/DiscImageProbe.h"
#include "core/disc/RawDiscSource.h"
#include "core/disc/SectorLayout.h"
#include "core/audio/AudioEngine.h"
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "core/scanner/FileScanner.h"
#include "ui/preview/PreviewPanel.h"
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <map>
#include <vector>
#include <juce_core/juce_core.h>
#include "TestHelpers.h"
using namespace sonicator;

namespace
{
// Env paths may list several images separated by ';'.
std::vector<juce::File> discImagesFromEnvironment()
{
    std::vector<juce::File> images;
    const auto raw = juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_IMAGE", {});
    for (const auto& part : juce::StringArray::fromTokens(raw, ";", {}))
        if (part.trim().isNotEmpty()) images.push_back(juce::File(part.trim()));
    return images;
}

// A .cue opens through its first data track, exactly as DiscBrowserService does. A .bin/.img
// opens through a case-insensitive sibling .cue when one exists, otherwise as a raw image.
DiscResult<ByteSourcePtr> openImageBytes(const juce::File& imageOrCue)
{
    DiscResult<ByteSourcePtr> result;
    juce::File cue = imageOrCue.getFileName().endsWithIgnoreCase(".cue") ? imageOrCue : juce::File();
    if (cue == juce::File())
    {
        const auto stem = imageOrCue.getFileNameWithoutExtension();
        const auto sibling = imageOrCue.getParentDirectory().getChildFile(stem + ".cue");
        if (sibling.existsAsFile()) cue = sibling;
        else
        {
            for (const auto& candidate : imageOrCue.getParentDirectory().findChildFiles(
                     juce::File::findFiles, false, "*.cue"))
                if (candidate.getFileNameWithoutExtension().equalsIgnoreCase(stem))
                {
                    cue = candidate;
                    break;
                }
        }
    }
    if (cue == juce::File())
    {
        const auto raw = openRawDisc(imageOrCue, {});
        if (! raw.ok()) { result.status = raw.status; result.error = raw.error; return result; }
        result.status = DiscStatus::ready;
        result.value = raw.value;
        return result;
    }
    const auto sheet = openCueSheet(cue, {}, {});
    if (! sheet.ok()) { result.status = sheet.status; result.error = sheet.error; return result; }
    const auto dataTracks = sheet.value.dataTrackIndices();
    if (dataTracks.empty())
    {
        result.status = DiscStatus::unsupported;
        result.error = "the cue sheet has no data track";
        return result;
    }
    if (dataTracks.size() > 1)
    {
        result.status = DiscStatus::ambiguous;
        result.error = "multiple data tracks; choose one";
        return result;
    }
    const auto layout = cueTrackLayout(sheet.value, dataTracks.front());
    if (! layout.ok()) { result.status = layout.status; result.error = layout.error; return result; }
    const auto raw = openRawDisc(layout.value.file, {});
    if (! raw.ok()) { result.status = raw.status; result.error = raw.error; return result; }
    TrackLayout track;
    track.fileOffset = layout.value.fileOffset;
    track.sectorCount = layout.value.sectorCount;
    track.profile = layout.value.profile;
    auto blocks = makeTrackSource(raw.value, track, {});
    if (! blocks.ok()) { result.status = blocks.status; result.error = blocks.error; return result; }
    result.status = DiscStatus::ready;
    result.value = std::make_shared<BlockByteSource>(blocks.value);
    return result;
}

struct RecordDecode
{
    DiscEntryId id;
    juce::String name;
    AkaiSampleHeader header;
    bool stereoPartner = false;
    uint64_t declaredBytes = 0;
};

// Decodes every Akai sample record, including the hidden right half of each stereo pair. The
// merged listing entry names only the left record, so the record name is read from the header
// name field at bytes 3..14 instead.
std::vector<RecordDecode> decodeAllRecords(AkaiFilesystem& fs, int& failures,
                                           juce::StringArray& failureLines, int& invalidHeaders,
                                           int& partitions, int& volumes, int& programs,
                                           int& mergedSamples, int& stereoEntries, int& header150,
                                           int& header192, int& audible, int& unsupportedEntries,
                                           int& invalidSampleRecords, juce::StringArray& invalidNames,
                                           juce::StringArray& unsupportedNames,
                                           bool decodeAudio = true,
                                           SampleAudioPtr* firstAudio = nullptr)
{
    std::vector<RecordDecode> records;
    auto decodeOne = [&](DiscEntryId id, bool partner, const juce::String& label)
    {
        const auto extent = fs.openFile(id, {});
        if (! extent.ok())
        {
            ++failures;
            if (failureLines.size() < 20) failureLines.add(label + ": " + extent.error);
            return;
        }
        const auto header = readAkaiSampleHeader(*extent.value, extent.value->size(), {});
        if (! header.ok())
        {
            std::array<std::byte, 0x8c> probe {};
            const auto probed = extent.value->readAt(0, probe, {});
            const auto word = [&](size_t at)
            {
                return (unsigned) std::to_integer<unsigned>(probe[at])
                     | ((unsigned) std::to_integer<unsigned>(probe[at + 1]) << 8);
            };
            const auto dword = [&](size_t at) { return (uint64_t) word(at) | ((uint64_t) word(at + 2) << 16); };
            const auto detail = probed.ok() && probed.value == probe.size()
                ? " (id=" + juce::String((int) std::to_integer<unsigned>(probe[0]))
                      + " frames=" + juce::String(dword(0x1a)) + " rate=" + juce::String(word(0x8a))
                      + " bytes=" + juce::String(extent.value->size()) + ")"
                : juce::String();
            // A zero rate field is an invalid record in the source itself, not a decoder fault.
            if (probed.ok() && probed.value == probe.size() && word(0x8a) == 0)
            {
                ++invalidHeaders;
                return;
            }
            ++failures;
            if (failureLines.size() < 20) failureLines.add(label + ": " + header.error + detail);
            return;
        }
        if (decodeAudio)
        {
            // Decode one record at a time and keep only its verdict, so a large disc does not
            // hold every sample at once. The first decoded source is kept for the app-path check.
            const auto audio = decodeAkaiSample(extent.value, extent.value->size(), {}, {});
            if (! audio.ok())
            {
                ++failures;
                if (failureLines.size() < 20) failureLines.add(label + ": " + audio.error);
                return;
            }
            float peak = 0;
            bool finite = true, inRange = true;
            for (float value : audio.value->left)
            {
                finite = finite && std::isfinite(value);
                inRange = inRange && std::abs(value) <= 1.0f;
                peak = juce::jmax(peak, std::abs(value));
            }
            if (! finite || ! inRange || audio.value->frames <= 0 || audio.value->sampleRate <= 0
                || audio.value->channels != 1
                || audio.value->left.size() != (size_t) audio.value->frames)
            {
                ++failures;
                if (failureLines.size() < 20) failureLines.add(label + ": decoded audio is not valid");
                return;
            }
            if (peak > 0.001f) ++audible;
            if (firstAudio != nullptr && *firstAudio == nullptr) *firstAudio = audio.value;
        }
        std::array<std::byte, 16> head {};
        juce::String name = label;
        if (const auto read = extent.value->readAt(0, head, {}); read.ok() && read.value == head.size())
            name = AkaiFilesystem::decodeName(head.data() + 3, 12);
        if (header.value.headerBytes == 150) ++header150;
        else if (header.value.headerBytes == 192) ++header192;
        records.push_back({ id, name, header.value, partner, extent.value->size() });
    };
    const auto roots = fs.list(fs.root(), {});
    REQUIRE(roots.ok());
    for (const auto& partition : roots.value.entries)
    {
        ++partitions;
        const auto listed = fs.list(partition.id, {});
        REQUIRE(listed.ok());
        for (const auto& volume : listed.value.entries)
        {
            ++volumes;
            const auto entries = fs.list(volume.id, {});
            REQUIRE(entries.ok());
            for (const auto& entry : entries.value.entries)
            {
                if (entry.kind == DiscEntryKind::container)
                {
                    ++programs;
                    continue;
                }
                if (entry.kind == DiscEntryKind::unknown || entry.capability.isNotEmpty())
                {
                    ++unsupportedEntries;
                    if (entry.capability.containsIgnoreCase("invalid start"))
                    {
                        ++invalidSampleRecords;
                        if (invalidNames.size() < 10) invalidNames.add(entry.displayName);
                    }
                    if (unsupportedNames.size() < 10)
                        unsupportedNames.add(entry.displayName + " (" + entry.capability + ")");
                    continue;
                }
                if (entry.kind != DiscEntryKind::file) continue;
                ++mergedSamples;
                if (entry.stereo) ++stereoEntries;
                const auto label = volume.displayName + "/" + entry.displayName;
                decodeOne(entry.id, false, label);
                if (entry.partner.value != 0) decodeOne(entry.partner, true, label + " (R)");
            }
        }
    }
    return records;
}

int intEnvironment(const char* name, int fallback)
{
    const auto value = juce::SystemStats::getEnvironmentVariable(name, {});
    return value.isNotEmpty() ? value.getIntValue() : fallback;
}

struct ReferenceWav
{
    juce::String name; // stem, trimmed
    int channels = 0;
    std::vector<int16_t> data;
};

// Minimal RIFF reader: PCM 16-bit mono/stereo only, so the comparison stays integer.
bool readReferenceWav(const juce::File& file, ReferenceWav& out, juce::String& error)
{
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block) || block.getSize() < 44) { error = "too short"; return false; }
    const auto* raw = (const unsigned char*) block.getData();
    const auto size = (size_t) block.getSize();
    const auto tag = [&](size_t at, const char* text) { return std::memcmp(raw + at, text, 4) == 0; };
    const auto le32 = [&](size_t at)
    {
        return (uint32_t) raw[at] | ((uint32_t) raw[at + 1] << 8)
             | ((uint32_t) raw[at + 2] << 16) | ((uint32_t) raw[at + 3] << 24);
    };
    const auto le16 = [&](size_t at) { return (unsigned) raw[at] | ((unsigned) raw[at + 1] << 8); };
    if (! tag(0, "RIFF") || ! tag(8, "WAVE")) { error = "not RIFF/WAVE"; return false; }
    size_t at = 12, dataAt = 0, dataBytes = 0;
    int format = 0, channels = 0, bits = 0;
    while (at + 8 <= size)
    {
        const auto chunkBytes = (size_t) le32(at + 4);
        if (tag(at, "fmt "))
        {
            if (chunkBytes < 16 || at + 8 + 16 > size) { error = "short fmt chunk"; return false; }
            format = (int) le16(at + 8);
            channels = (int) le16(at + 10);
            bits = (int) le16(at + 22);
        }
        else if (tag(at, "data"))
        {
            dataAt = at + 8;
            dataBytes = juce::jmin(chunkBytes, size - dataAt);
        }
        at += 8 + chunkBytes + (chunkBytes & 1);
    }
    if (format != 1 || bits != 16 || channels < 1 || channels > 2 || dataBytes == 0)
    {
        error = "not PCM 16-bit mono/stereo";
        return false;
    }
    out.name = file.getFileNameWithoutExtension().trim();
    out.channels = channels;
    const auto frames = dataBytes / ((size_t) channels * 2);
    out.data.resize(frames * (size_t) channels);
    std::memcpy(out.data.data(), raw + dataAt, frames * (size_t) channels * 2);
    return true;
}
} // namespace

// Hidden by default ([.] tag). Set SONICATOR_DISC_IMAGE to one or more real read-only images
// separated by ';'. The test never writes to them and verifies size, mtime and SHA-256 after.
TEST_CASE("Opt-in real disc image catalogue", "[.disc-corpus]")
{
    const auto images = discImagesFromEnvironment();
    REQUIRE_FALSE(images.empty());
    for (const auto& file : images)
    {
        REQUIRE(file.existsAsFile());
    }
    for (const auto& file : images)
    {
        const auto size = file.getSize();
        const auto modified = file.getLastModificationTime();
        const auto hash = FileScanner::computeFileHash(file);
        INFO("image: " << file.getFullPathName());

        const auto raw = openImageBytes(file);
        INFO(raw.error);
        REQUIRE(raw.ok());
        const auto probe = probeDisc(*raw.value, {}, {});
        INFO(probe.error);
        REQUIRE(probe.ok());
        REQUIRE(probe.value.kind == DiscImageKind::akaiRaw);
        const auto opened = AkaiFilesystem::open(raw.value, {}, {});
        INFO(opened.error);
        REQUIRE(opened.ok());
        auto& fs = *opened.value;

        int failures = 0, partitions = 0, volumes = 0, programs = 0, mergedSamples = 0;
        int stereoEntries = 0, header150 = 0, header192 = 0, invalidHeaders = 0;
        int audible = 0, unsupportedEntries = 0, invalidSampleRecords = 0;
        juce::StringArray unsupportedNames, invalidNames, failureLines;
        SampleAudioPtr firstAudio;
        const auto records = decodeAllRecords(fs, failures, failureLines, invalidHeaders, partitions,
                                              volumes, programs, mergedSamples, stereoEntries,
                                              header150, header192, audible, unsupportedEntries,
                                              invalidSampleRecords, invalidNames, unsupportedNames,
                                              true, &firstAudio);
        INFO("failures:\n" << failureLines.joinIntoString("\n"));
        INFO("partitions=" << partitions << " volumes=" << volumes << " programs=" << programs
             << " samples=" << mergedSamples << " records=" << records.size()
             << " stereo=" << stereoEntries << " headers150=" << header150
             << " headers192=" << header192 << " invalidHeaders=" << invalidHeaders
             << " unsupportedEntries=" << unsupportedEntries << " audible=" << audible);
        REQUIRE(failures == 0);
        // A handful of records on a real disc carry a zero rate field: invalid in the source.
        const auto allRecords = (int) records.size() + invalidHeaders;
        REQUIRE(invalidHeaders <= allRecords / 100);
        REQUIRE(partitions >= 1);
        REQUIRE(volumes >= 1);
        REQUIRE_FALSE(records.empty());
        INFO("unsupported: " << unsupportedNames.joinIntoString(" | "));
        INFO("sample records with an invalid start or size: " << invalidNames.joinIntoString(", "));
        // Other Akai file types (QL1, TL1, effects and drum files) are expected here; a sample
        // record with an invalid start or size is not.
        // Real discs carry a few damaged records (Real Mega Rhythm has 59 of 1,911); a change
        // that invalidates a large share of the catalogue must still fail here.
        REQUIRE(invalidSampleRecords * 10 <= mergedSamples);
        REQUIRE(audible * 100 >= (int) records.size() * 95);

        // A verified stereo pair decodes as two channels through the merged entry.
        bool checkedStereo = false;
        const auto roots = fs.list(fs.root(), {});
        REQUIRE(roots.ok());
        for (const auto& partition : roots.value.entries)
        {
            const auto listed = fs.list(partition.id, {});
            for (const auto& volume : listed.value.entries)
            {
                const auto entries = fs.list(volume.id, {});
                for (const auto& entry : entries.value.entries)
                {
                    if (entry.kind != DiscEntryKind::file || ! entry.stereo) continue;
                    const auto stereo = fs.decodeSample(entry.id, {});
                    INFO(entry.displayName << ": status=" << (int) stereo.status << " error=" << stereo.error);
                    REQUIRE(stereo.ok());
                    REQUIRE(stereo.value->channels == 2);
                    REQUIRE(stereo.value->right.size() == (size_t) stereo.value->frames);
                    checkedStereo = true;
                    break;
                }
                if (checkedStereo) break;
            }
            if (checkedStereo) break;
        }
        if (intEnvironment("SONICATOR_DISC_EXPECT_STEREO", 0) == 1) REQUIRE(checkedStereo);

        if (const auto expected = intEnvironment("SONICATOR_DISC_EXPECT_PARTITIONS", 0); expected > 0)
            REQUIRE(partitions == expected);
        if (const auto expected = intEnvironment("SONICATOR_DISC_EXPECT_VOLUMES", 0); expected > 0)
            REQUIRE(volumes == expected);
        if (const auto expected = intEnvironment("SONICATOR_DISC_EXPECT_PROGRAMS", 0); expected > 0)
            REQUIRE(programs == expected);

        // The catalog pass stores metadata rows on the worker's temp database only.
        TestDirectory t;
        const auto catalog = t.dir.getChildFile("catalog.sqlite");
        Database db(catalog);
        SampleRepository repo(db);
        repo.createTables();
        const auto summary = indexDiscContents(repo, { file }, {});
        INFO("index: tried=" << summary.imagesTried << " indexed=" << summary.imagesIndexed
             << " skipped=" << summary.imagesSkipped << " failed=" << summary.imagesFailed
             << " entries=" << summary.entries << " error=" << summary.lastError);
        REQUIRE(summary.imagesFailed == 0);
        REQUIRE(summary.imagesIndexed == 1);
        REQUIRE(summary.entries > 0);

        REQUIRE(file.getSize() == size);
        REQUIRE(file.getLastModificationTime() == modified);
        REQUIRE(FileScanner::computeFileHash(file) == hash);

        // The app path: the decoded source plays through the ordinary transport and exports its
        // exact frames from the preview panel.
        {
            TestDirectory exports;
            AudioEngine engine(false);
            PreviewPanel preview(engine, exports.dir.getChildFile("cache"),
                                 exports.dir.getChildFile("exports"));
            const auto& sample = records.front();
            REQUIRE(firstAudio != nullptr);
            REQUIRE(preview.loadDiscSample(file, firstAudio != nullptr ? sample.name : juce::String(),
                                           firstAudio, false, 0, 0, false, false));
            REQUIRE(engine.isDiscMode());
            REQUIRE(preview.waveformLength()
                    == Catch::Approx((double) firstAudio->frames / firstAudio->sampleRate).margin(1.0e-6));
            preview.selectRange(0.0, preview.waveformLength());
            REQUIRE(preview.waitForSelectionPreparation(10000));
            REQUIRE(preview.isSelectionReady());
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(preview.preparedSelection()));
            REQUIRE(reader != nullptr);
            REQUIRE(reader->lengthInSamples == firstAudio->frames);
            REQUIRE(reader->sampleRate == firstAudio->sampleRate);
            juce::AudioBuffer<float> decoded(1, (int) firstAudio->frames);
            REQUIRE(reader->read(&decoded, 0, (int) firstAudio->frames, 0, true, false));
            // The WAV writer quantises to 16 bits, so a round trip may differ by one LSB.
            float maxDifference = 0;
            for (int i = 0; i < (int) firstAudio->frames; ++i)
                maxDifference = juce::jmax(maxDifference,
                                           std::abs(decoded.getSample(0, i) - firstAudio->left[(size_t) i]));
            REQUIRE(maxDifference <= 1.0f / 32768.0f);
            preview.clearDiscSource();
            REQUIRE_FALSE(engine.isDiscMode());
        }
    }
}

// Hidden by default ([.] tag). Compares decoded disc audio, byte for byte, against WAVs produced
// by an independent converter (SONICATOR_DISC_REFERENCE_DIR). Only 16-bit PCM references are
// used; the disc range is [start, end) from the sample header, and the reference keeps the last
// frame the converter zero-padded, so a reference one frame longer is accepted.
TEST_CASE("Opt-in Akai PCM matches a reference conversion", "[.disc-reference]")
{
    const auto images = discImagesFromEnvironment();
    REQUIRE_FALSE(images.empty());
    const juce::File referenceDir(
        juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_REFERENCE_DIR", {}));
    REQUIRE(referenceDir.isDirectory());

    std::map<juce::String, std::vector<ReferenceWav>> references;
    int skippedFormat = 0;
    for (const auto& candidate : referenceDir.findChildFiles(juce::File::findFiles, true, "*"))
    {
        if (! candidate.getFileName().endsWithIgnoreCase(".wav")) continue;
        ReferenceWav wav;
        juce::String error;
        if (! readReferenceWav(candidate, wav, error)) { ++skippedFormat; continue; }
        references[wav.name.toUpperCase()].push_back(std::move(wav));
    }
    INFO("references: " << references.size() << " names, skippedFormat=" << skippedFormat);
    REQUIRE_FALSE(references.empty());

    int compared = 0, mismatches = 0, missingReference = 0, ambiguous = 0, lengthOther = 0;
    int stereoReferences = 0, truncatedByConverter = 0, emptyComparisons = 0;
    juce::StringArray stereoReferenceNames;
    juce::StringArray mismatchLines;

    // A name repeated on any image makes the reference match ambiguous, so names are counted
    // across every image before anything is compared.
    std::map<juce::String, int> globalNameCounts;
    for (const auto& file : images)
    {
        const auto raw = openImageBytes(file);
        REQUIRE(raw.ok());
        const auto opened = AkaiFilesystem::open(raw.value, {}, {});
        REQUIRE(opened.ok());
        int failures = 0, partitions = 0, volumes = 0, programs = 0, mergedSamples = 0;
        int stereoEntries = 0, header150 = 0, header192 = 0, invalidHeaders = 0;
        int audible = 0, unsupportedEntries = 0, invalidSampleRecords = 0;
        juce::StringArray unsupportedNames, invalidNames, failureLines;
        const auto records = decodeAllRecords(*opened.value, failures, failureLines, invalidHeaders,
                                              partitions, volumes, programs, mergedSamples,
                                              stereoEntries, header150, header192, audible,
                                              unsupportedEntries, invalidSampleRecords, invalidNames,
                                              unsupportedNames, false);
        REQUIRE(failures == 0);
        for (const auto& record : records) ++globalNameCounts[record.name.trim().toUpperCase()];
    }

    for (const auto& file : images)
    {
        INFO("image: " << file.getFullPathName());
        const auto raw = openImageBytes(file);
        REQUIRE(raw.ok());
        const auto opened = AkaiFilesystem::open(raw.value, {}, {});
        REQUIRE(opened.ok());
        auto& fs = *opened.value;

        int failures = 0, partitions = 0, volumes = 0, programs = 0, mergedSamples = 0;
        int stereoEntries = 0, header150 = 0, header192 = 0, invalidHeaders = 0;
        int audible = 0, unsupportedEntries = 0, invalidSampleRecords = 0;
        juce::StringArray unsupportedNames, invalidNames, failureLines;
        const auto records = decodeAllRecords(fs, failures, failureLines, invalidHeaders,
                                              partitions, volumes, programs, mergedSamples,
                                              stereoEntries, header150, header192, audible,
                                              unsupportedEntries, invalidSampleRecords, invalidNames,
                                              unsupportedNames, false);
        INFO("failures:\n" << failureLines.joinIntoString("\n"));
        REQUIRE(failures == 0);


        for (const auto& record : records)
        {
            const auto key = record.name.trim().toUpperCase();
            if (key.isEmpty()) continue;
            const auto found = references.find(key);
            if (found == references.end()) { ++missingReference; continue; }
            if (found->second.size() > 1 || globalNameCounts[key] > 1) { ++ambiguous; continue; }
            const auto& reference = found->second.front();
            if (reference.channels != 1)
            {
                // A stereo reference would need both halves of a pair under one name; the disc
                // names each half, so there is nothing to pair it with here.
                ++stereoReferences;
                if (stereoReferenceNames.size() < 10) stereoReferenceNames.add(key);
                continue;
            }
            const auto start = record.header.startFrame;
            const auto end = record.header.endFrame;
            if (end <= start || end > record.header.frames)
            {
                ++mismatches;
                if (mismatchLines.size() < 20)
                    mismatchLines.add(key + ": header range " + juce::String(start) + ".."
                                      + juce::String(end) + " outside " + juce::String(record.header.frames));
                continue;
            }
            const auto extent = fs.openFile(record.id, {});
            REQUIRE(extent.ok());
            const auto pcm = readAkaiPcm16(*extent.value, record.header, {});
            INFO(key << ": " << pcm.error);
            REQUIRE(pcm.ok());
            const auto frames = (size_t) (end - start);
            const auto referenceFrames = reference.data.size();
            if (referenceFrames != frames && referenceFrames != frames + 1) ++lengthOther;
            // The reference converter reads only whole 8192-byte blocks up to the one holding the
            // playback end, then zero-pads to the declared length. Compare the audio it actually
            // produced; the discarded tail is at most one block.
            const auto neededBytes = (uint64_t) record.header.headerBytes + (uint64_t) end * 2;
            const auto wholeBlocks = (neededBytes / 8192) * 8192;
            const auto converterFrames = wholeBlocks > record.header.headerBytes
                ? (size_t) ((wholeBlocks - record.header.headerBytes) / 2) : 0;
            if (converterFrames < frames)
            {
                ++truncatedByConverter;
                REQUIRE(frames - converterFrames <= 4096);
            }
            const auto available = (size_t) juce::jmax<int64_t>(0, (int64_t) converterFrames - (int64_t) start);
            const auto count = juce::jmin(frames, juce::jmin(referenceFrames, available));
            size_t firstDifference = count;
            for (size_t i = 0; i < count; ++i)
                if (pcm.value[(size_t) start + i] != reference.data[i]) { firstDifference = i; break; }
            if (count == 0) { ++emptyComparisons; continue; }
            ++compared;
            if (firstDifference != count)
            {
                ++mismatches;
                if (mismatchLines.size() < 20)
                    mismatchLines.add(key + ": first difference at frame " + juce::String((int) firstDifference)
                                      + " (disc " + juce::String((int) pcm.value[(size_t) start + firstDifference])
                                      + " vs reference " + juce::String((int) reference.data[firstDifference]) + ")");
            }
        }
    }
    INFO("compared=" << compared << " mismatches=" << mismatches << " missingReference=" << missingReference
         << " ambiguous=" << ambiguous << " lengthOther=" << lengthOther
         << " stereoReferences=" << stereoReferences
         << " truncatedByConverter=" << truncatedByConverter
         << " emptyComparisons=" << emptyComparisons);
    INFO("stereo references skipped: " << stereoReferenceNames.joinIntoString(", "));
    INFO("mismatches:\n" << mismatchLines.joinIntoString("\n"));
    REQUIRE(compared >= 500);
    // A reference whose audio would start after its whole-block cut is unverifiable; a few of
    // them exist and are counted, not compared.
    REQUIRE(emptyComparisons * 100 <= compared);
    REQUIRE(lengthOther == 0);
    // The reference directory is a mixed conversion of several libraries, so a same-named WAV can
    // be a different take: those pairs differ from the start of their audio, while a decoder
    // regression would break most pairs. The exact-match share is the guard.
    REQUIRE(mismatches * 100 <= compared * 2);
}
