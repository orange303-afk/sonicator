#include <catch2/catch_test_macros.hpp>
#include "core/disc/AkaiFilesystem.h"
#include "core/disc/CueSheet.h"
#include "core/disc/DiscBrowserService.h"
#include "core/disc/DiscImageProbe.h"
#include "core/disc/RawDiscSource.h"
#include "core/disc/SectorLayout.h"
#include "core/scanner/FileScanner.h"
#include "core/extract/DiscExtract.h"
#include "core/extract/ExtractService.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstring>
#include <vector>
#include "TestHelpers.h"
using namespace sonicator;

namespace
{
constexpr size_t block = 8192;
void putLe16(std::vector<unsigned char>& b, size_t at, uint16_t v)
{
    b[at] = (unsigned char) (v & 0xff); b[at + 1] = (unsigned char) (v >> 8);
}
void putLe24(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 3; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
void putLe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
unsigned char akaiChar(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char) (c - '0');
    if (c >= 'A' && c <= 'Z') return (unsigned char) (0x0b + c - 'A');
    if (c == ' ') return 0x0a;
    if (c == '.') return 0x28;
    if (c == '-') return 0x27;
    return 0x0a;
}
void putAkaiName(std::vector<unsigned char>& b, size_t at, const char* name)
{
    std::memset(b.data() + at, 0x0a, 12);
    for (size_t i = 0; name[i] != 0 && i < 12; ++i) b[at + i] = akaiChar(name[i]);
}
void putVolume(std::vector<unsigned char>& b, size_t tableBase, int slot, const char* name,
               uint16_t type, uint16_t start)
{
    const size_t at = tableBase + 0xca + (size_t) slot * 16;
    putAkaiName(b, at, name);
    putLe16(b, at + 12, type);
    putLe16(b, at + 14, start);
}
void putRecord(std::vector<unsigned char>& b, size_t directoryAt, int index, const char* name,
               uint8_t type, uint32_t bytes, uint16_t start)
{
    const size_t at = directoryAt + (size_t) index * 24;
    putAkaiName(b, at, name);
    b[at + 0x10] = type;
    putLe24(b, at + 0x11, bytes);
    putLe16(b, at + 0x14, start);
}
void putFat(std::vector<unsigned char>& b, uint16_t blockIndex, uint16_t value)
{
    putLe16(b, 0x70a + (size_t) blockIndex * 2, value);
}
// One S1000 volume (type 1): BASS (audible), TINK (silent), a PAIR .L/.R stereo pair and a program.
juce::File writeAkaiImage(TestDirectory& t, const juce::String& name)
{
    std::vector<unsigned char> image(40 * block, 0);
    for (int i = 0; i < 3; ++i) putLe16(image, 0x70a + (size_t) i * 2, 0x40); // reserved markers
    image[0x4500] = 1; image[0x4501] = 1;
    putLe16(image, 0x4502, 40);
    putVolume(image, 0, 0, "VOL", 1, 1);
    const size_t directory = 1 * block;
    putRecord(image, directory, 0, "BASS", 0x73, 150 + 8 * 2, 4);
    putRecord(image, directory, 1, "TINK", 0x73, 150 + 8 * 2, 5);
    putRecord(image, directory, 2, "PAIR .L", 0x73, 150 + 4 * 2, 6);
    putRecord(image, directory, 3, "PAIR .R", 0x73, 150 + 4 * 2, 7);
    putRecord(image, directory, 4, "PROG", 0x70, 0, 0);
    putFat(image, 4, 0x00c0);
    putFat(image, 5, 0x00c0);
    putFat(image, 6, 0x00c0);
    putFat(image, 7, 0x00c0);
    auto putSample = [&image](uint16_t atBlock, uint32_t frames, const short* values, int count)
    {
        const size_t at = (size_t) atBlock * block;
        image[at] = 3;
        putLe32(image, at + 0x1a, frames);
        putLe16(image, at + 0x8a, 44100);
        for (int i = 0; i < count; ++i) putLe16(image, at + 150 + (size_t) i * 2, (uint16_t) values[i]);
    };
    const short bass[8] = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 };
    const short silent[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const short left[4] = { 1000, 2000, 3000, 4000 };
    const short right[4] = { -1000, -2000, -3000, -4000 };
    putSample(4, 8, bass, 8);
    putSample(5, 8, silent, 8);
    putSample(6, 4, left, 4);
    putSample(7, 4, right, 4);
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    if (auto out = file.createOutputStream()) { out->write(image.data(), (int) image.size()); out->flush(); }
    return file;
}
int wavChannels(const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    return reader == nullptr ? 0 : (int) reader->numChannels;
}
int64_t wavFrames(const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    return reader == nullptr ? -1 : reader->lengthInSamples;
}
juce::StringArray wavFiles(const juce::File& folder)
{
    juce::StringArray names;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.wav"))
        names.add(file.getFileName());
    names.sort(true);
    return names;
}
} // namespace

TEST_CASE("Whole Akai image extraction writes every audible sample", "[extract][disc]")
{
    TestDirectory t;
    const auto image = writeAkaiImage(t, "disc.iso");
    ExtractService service;
    REQUIRE(service.startDisc({ { image, {} } }, {}));
    REQUIRE(service.waitUntilFinished(120000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    const auto& result = results.front();
    INFO(result.error << " " << result.problems.joinIntoString(" | "));
    REQUIRE(result.state == ExtractFileState::extracted);
    REQUIRE(result.written == 2);
    REQUIRE(result.silent == 1);
    const auto folder = t.dir.getChildFile("disc samples");
    REQUIRE(result.folder == folder);
    REQUIRE(wavFiles(folder) == juce::StringArray({ "001 BASS.wav", "003 PAIR.wav" }));
    REQUIRE(wavChannels(folder.getChildFile("001 BASS.wav")) == 1);
    REQUIRE(wavFrames(folder.getChildFile("001 BASS.wav")) == 8);
    REQUIRE(wavChannels(folder.getChildFile("003 PAIR.wav")) == 2);
    REQUIRE(wavFrames(folder.getChildFile("003 PAIR.wav")) == 4);
}

TEST_CASE("Selected disc entries extract only those", "[extract][disc]")
{
    TestDirectory t;
    const auto image = writeAkaiImage(t, "disc.iso");
    // The ids come from the same listing the browser shows.
    const auto raw = openRawDisc(image, {});
    REQUIRE(raw.ok());
    const auto opened = AkaiFilesystem::open(raw.value, {}, {});
    REQUIRE(opened.ok());
    auto& fs = *opened.value;
    const auto partitions = fs.list(fs.root(), {});
    REQUIRE(partitions.value.entries.size() == 1);
    const auto volumes = fs.list(partitions.value.entries[0].id, {});
    const auto entries = fs.list(volumes.value.entries[0].id, {});
    DiscExtractJob job { image, {} };
    for (const auto& entry : entries.value.entries)
        if (entry.kind == DiscEntryKind::file && entry.displayName == "BASS")
            job.entries.push_back({ entry.id, entry.partner, entry.displayName });
    REQUIRE(job.entries.size() == 1);
    ExtractService service;
    REQUIRE(service.startDisc({ job }, {}));
    REQUIRE(service.waitUntilFinished(120000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    INFO(results.front().error << " " << results.front().problems.joinIntoString(" | "));
    REQUIRE(results.front().written == 1);
    REQUIRE(wavFiles(t.dir.getChildFile("disc samples")) == juce::StringArray({ "001 BASS.wav" }));
}

TEST_CASE("A broken image does not stop the batch", "[extract][disc]")
{
    TestDirectory t;
    const auto good = writeAkaiImage(t, "good.iso");
    auto broken = t.dir.getChildFile("broken.iso");
    broken.replaceWithText("not a disc image");
    ExtractService service;
    REQUIRE(service.startDisc({ { broken, {} }, { good, {} } }, {}));
    REQUIRE(service.waitUntilFinished(120000));
    const auto results = service.results();
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].state == ExtractFileState::failed);
    REQUIRE(results[0].error.isNotEmpty());
    REQUIRE(results[1].state == ExtractFileState::extracted);
    REQUIRE(results[1].written == 2);
}

TEST_CASE("Cancelling image extraction keeps finished files", "[extract][disc]")
{
    TestDirectory t;
    std::vector<unsigned char> image(40 * block, 0);
    for (int i = 0; i < 3; ++i) putLe16(image, 0x70a + (size_t) i * 2, 0x40);
    image[0x4500] = 1; image[0x4501] = 1;
    putLe16(image, 0x4502, 40);
    putVolume(image, 0, 0, "VOL", 1, 1);
    for (int i = 0; i < 20; ++i)
        putRecord(image, 1 * block, i, ("S" + juce::String(i)).toRawUTF8(), 0x73, 150 + 2 * 2, (uint16_t) (4 + i));
    for (int i = 0; i < 20; ++i)
    {
        putFat(image, (uint16_t) (4 + i), 0x00c0);
        const size_t at = (size_t) (4 + i) * block;
        image[at] = 3;
        putLe32(image, at + 0x1a, 2);
        putLe16(image, at + 0x8a, 44100);
        putLe16(image, at + 150, 1000);
        putLe16(image, at + 152, 2000);
    }
    auto file = t.dir.getChildFile("many.iso");
    if (auto out = file.createOutputStream()) { out->write(image.data(), (int) image.size()); out->flush(); }
    ExtractService service;
    REQUIRE(service.startDisc({ { file, {} } }, {}));
    for (int i = 0; i < 5000 && service.progress().written == 0; ++i) juce::Thread::sleep(1);
    service.cancel();
    REQUIRE(service.waitUntilFinished(120000));
    const auto progress = service.progress();
    REQUIRE(progress.cancelled);
    REQUIRE(progress.written >= 1);
    REQUIRE(progress.written < 20); // cancel stops the batch before every sample is written
    const auto folder = t.dir.getChildFile("many samples");
    if (folder.isDirectory())
    {
        REQUIRE(wavFiles(folder).size() == progress.written);
        REQUIRE(folder.findChildFiles(juce::File::findFiles, false, "*.part").isEmpty());
    }
}

// Opt-in ([.] tag): extracts five records of a real image into a temporary folder and compares
// every written WAV with the decoder, byte for byte. The image is opened read-only.
TEST_CASE("Opt-in real image extraction matches the decoder", "[.extract-corpus]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_IMAGE", {});
    REQUIRE(path.isNotEmpty());
    const juce::File image(path);
    REQUIRE(image.existsAsFile());
    const auto size = image.getSize();
    const auto modified = image.getLastModificationTime();
    const auto hash = FileScanner::computeFileHash(image);

    DiscResult<ByteSourcePtr> raw;
    if (image.hasFileExtension("cue"))
    {
        const auto sheet = openCueSheet(image, {}, {});
        REQUIRE(sheet.ok());
        const auto tracks = sheet.value.dataTrackIndices();
        REQUIRE(tracks.size() == 1);
        const auto layout = cueTrackLayout(sheet.value, tracks.front());
        REQUIRE(layout.ok());
        const auto bin = openRawDisc(layout.value.file, {});
        REQUIRE(bin.ok());
        TrackLayout track;
        track.fileOffset = layout.value.fileOffset;
        track.sectorCount = layout.value.sectorCount;
        track.profile = layout.value.profile;
        auto blocks = makeTrackSource(bin.value, track, {});
        REQUIRE(blocks.ok());
        raw.status = DiscStatus::ready;
        raw.value = std::make_shared<BlockByteSource>(blocks.value);
    }
    REQUIRE(raw.ok());
    const auto opened = AkaiFilesystem::open(raw.value, {}, {});
    REQUIRE(opened.ok());
    auto& fs = *opened.value;
    const auto partitions = fs.list(fs.root(), {});
    REQUIRE(partitions.ok());
    std::vector<DiscExtractEntry> entries;
    for (const auto& partition : partitions.value.entries)
    {
        const auto volumes = fs.list(partition.id, {});
        for (const auto& volume : volumes.value.entries)
        {
            const auto listed = fs.list(volume.id, {});
            for (const auto& entry : listed.value.entries)
                if (entry.kind == DiscEntryKind::file && entry.capability.isEmpty()
                    && entries.size() < 5)
                    entries.push_back({ entry.id, entry.partner, entry.displayName });
            if (entries.size() >= 5) break;
        }
        if (entries.size() >= 5) break;
    }
    REQUIRE(entries.size() == 5);

    TestDirectory output;
    ExtractOptions options;
    options.sameFolder = false;
    options.outputFolder = output.dir;
    ExtractService service;
    REQUIRE(service.startDisc({ { image, entries } }, options));
    REQUIRE(service.waitUntilFinished(120000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    INFO(results.front().error << " " << results.front().problems.joinIntoString(" | "));
    REQUIRE(results.front().failed == 0);
    const auto folder = results.front().folder;
    REQUIRE(folder.isDirectory());

    juce::AudioFormatManager formats; formats.registerBasicFormats();
    int compared = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto decoded = fs.decodeSample(entries[i].id, {});
        REQUIRE(decoded.ok());
        const auto expected = folder.getChildFile(extractSampleFileName((int) i + 1, entries[i].name));
        if (! expected.existsAsFile()) continue; // silent samples are not written
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(expected));
        REQUIRE(reader != nullptr);
        REQUIRE(reader->lengthInSamples == decoded.value->frames);
        REQUIRE((int) reader->numChannels == decoded.value->channels);
        juce::AudioBuffer<float> data((int) reader->numChannels, (int) reader->lengthInSamples);
        REQUIRE(reader->read(&data, 0, (int) reader->lengthInSamples, 0, true, true));
        for (int channel = 0; channel < (int) reader->numChannels; ++channel)
            for (int frame = 0; frame < (int) reader->lengthInSamples; ++frame)
            {
                const auto& reference = channel == 1 ? decoded.value->right : decoded.value->left;
                REQUIRE(std::abs(data.getSample(channel, frame) - reference[(size_t) frame]) <= 1.0f / 32768.0f);
            }
        ++compared;
    }
    REQUIRE(compared >= 3);
    REQUIRE(image.getSize() == size);
    REQUIRE(image.getLastModificationTime() == modified);
    REQUIRE(FileScanner::computeFileHash(image) == hash);
}
