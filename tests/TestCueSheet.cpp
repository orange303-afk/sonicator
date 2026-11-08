#include <catch2/catch_test_macros.hpp>
#include "core/disc/CueSheet.h"
#include "TestHelpers.h"
#include <cstring>
using namespace sonicator;

namespace
{
juce::File writeSized(TestDirectory& t, const juce::String& name, uint64_t bytes)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    auto out = file.createOutputStream();
    REQUIRE(out != nullptr);
    std::vector<char> zeros(8192, 0);
    while (bytes > 0)
    {
        const auto chunk = (size_t) juce::jmin<uint64_t>(bytes, zeros.size());
        out->write(zeros.data(), (int) chunk);
        bytes -= chunk;
    }
    out->flush();
    return file;
}
juce::File writeCue(TestDirectory& t, const juce::String& name, const juce::String& text)
{
    auto file = t.dir.getChildFile(name);
    file.replaceWithText(text);
    return file;
}
CueSheet resolveText(const juce::File& cue)
{
    const auto opened = openCueSheet(cue, {}, {});
    INFO(opened.error);
    REQUIRE(opened.ok());
    return opened.value;
}
} // namespace

TEST_CASE("A CUE with an audio track before a Mode1 data track resolves exact offsets", "[disc-cue]")
{
    TestDirectory t;
    writeSized(t, "disc.bin", 300 * 2352);
    auto cue = writeCue(t, "disc.cue", R"(FILE "disc.bin" BINARY
  TRACK 01 AUDIO
    INDEX 01 00:00:00
  TRACK 02 MODE1/2352
    INDEX 00 00:01:00
    INDEX 01 00:02:00
)");
    const auto sheet = resolveText(cue);
    REQUIRE(sheet.tracks.size() == 2);
    REQUIRE(sheet.dataTrackIndices().size() == 1);
    REQUIRE_FALSE(sheet.tracks[0].isData());
    const auto layout = cueTrackLayout(sheet, 1);
    INFO(layout.error);
    REQUIRE(layout.ok());
    REQUIRE(layout.value.profile == SectorProfile::mode1_2352);
    REQUIRE(layout.value.fileOffset == 150 * 2352);
    REQUIRE(layout.value.sectorCount == 150);
    REQUIRE(layout.value.file == t.dir.getChildFile("disc.bin"));
    REQUIRE(cueTrackLayout(sheet, 0).status == DiscStatus::unsupported); // CDDA is metadata-only
}

TEST_CASE("PREGAP is recorded but never added to the stored file offset", "[disc-cue]")
{
    TestDirectory t;
    writeSized(t, "pregap.bin", 200 * 2352);
    auto cue = writeCue(t, "pregap.cue", R"(FILE pregap.bin BINARY
  TRACK 01 MODE1/2352
    PREGAP 00:02:00
    INDEX 01 00:01:00
)");
    const auto sheet = resolveText(cue);
    REQUIRE(sheet.tracks[0].pregapFrames == 150);
    const auto layout = cueTrackLayout(sheet, 0);
    REQUIRE(layout.ok());
    REQUIRE(layout.value.fileOffset == 75 * 2352);
    REQUIRE(layout.value.sectorCount == 125);
}

TEST_CASE("Separate 2048 files resolve per file and track boundaries", "[disc-cue]")
{
    TestDirectory t;
    writeSized(t, "a.bin", 20 * 2048);
    writeSized(t, "b.bin", 30 * 2048);
    auto cue = writeCue(t, "two.cue", R"(FILE "a.bin" BINARY
  TRACK 01 MODE1/2048
    INDEX 01 00:00:00
FILE "b.bin" BINARY
  TRACK 02 MODE1/2048
    INDEX 01 00:00:10
)");
    const auto sheet = resolveText(cue);
    REQUIRE(sheet.dataTrackIndices().size() == 2);
    const auto first = cueTrackLayout(sheet, 0);
    REQUIRE(first.ok());
    REQUIRE(first.value.sectorCount == 20);
    const auto second = cueTrackLayout(sheet, 1);
    REQUIRE(second.ok());
    REQUIRE(second.value.fileOffset == 10 * 2048);
    REQUIRE(second.value.sectorCount == 20);
    REQUIRE(second.value.file.getFileName() == "b.bin");
}

TEST_CASE("Mixed 2048/2352 tracks in one FILE are refused", "[disc-cue]")
{
    TestDirectory t;
    writeSized(t, "mixed.bin", 100 * 2352);
    auto cue = writeCue(t, "mixed.cue", R"(FILE "mixed.bin" BINARY
  TRACK 01 MODE1/2048
    INDEX 01 00:00:00
  TRACK 02 MODE1/2352
    INDEX 01 00:00:50
)");
    const auto opened = openCueSheet(cue, {}, {});
    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status == DiscStatus::unsupported);
}

TEST_CASE("Unsafe, missing and non-binary FILE references are rejected", "[disc-cue]")
{
    TestDirectory t;
    writeSized(t, "ok.bin", 10 * 2048);
    for (const auto* reference : { "../outside.bin", "/absolute.bin", "http://example.com/x.bin" })
    {
        auto cue = writeCue(t, "unsafe.cue", juce::String("FILE \"") + reference + "\" BINARY\n"
            "  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
        const auto opened = openCueSheet(cue, {}, {});
        REQUIRE_FALSE(opened.ok());
        REQUIRE(opened.status == DiscStatus::unsupported);
    }
    auto missing = writeCue(t, "missing.cue", "FILE absent.bin BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
    const auto missingOpen = openCueSheet(missing, {}, {});
    REQUIRE_FALSE(missingOpen.ok());
    REQUIRE(missingOpen.status == DiscStatus::malformed);
    auto wave = writeCue(t, "wave.cue", "FILE ok.bin WAVE\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
    const auto waveOpen = openCueSheet(wave, {}, {});
    REQUIRE_FALSE(waveOpen.ok());
    REQUIRE(waveOpen.status == DiscStatus::unsupported);
}

TEST_CASE("Malformed times, indexes and track starts are rejected", "[disc-cue]")
{
    TestDirectory t;
    writeSized(t, "bad.bin", 100 * 2048);
    auto time = writeCue(t, "time.cue", "FILE bad.bin BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:61:00\n");
    REQUIRE(openCueSheet(time, {}, {}).status == DiscStatus::malformed);
    auto frame = writeCue(t, "frame.cue", "FILE bad.bin BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:75\n");
    REQUIRE(openCueSheet(frame, {}, {}).status == DiscStatus::malformed);
    auto noIndex = writeCue(t, "noindex.cue", "FILE bad.bin BINARY\n  TRACK 01 MODE1/2048\n");
    REQUIRE(openCueSheet(noIndex, {}, {}).status == DiscStatus::malformed);
    auto highIndex = writeCue(t, "highindex.cue", "FILE bad.bin BINARY\n  TRACK 01 MODE1/2048\n    INDEX 02 00:00:00\n");
    REQUIRE(openCueSheet(highIndex, {}, {}).status == DiscStatus::unsupported);
    auto duplicate = writeCue(t, "duplicate.cue", "FILE bad.bin BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n"
        "  TRACK 01 MODE1/2048\n    INDEX 01 00:00:10\n");
    REQUIRE(openCueSheet(duplicate, {}, {}).status == DiscStatus::malformed);
    auto backwards = writeCue(t, "backwards.cue", "FILE bad.bin BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:10\n"
        "  TRACK 02 MODE1/2048\n    INDEX 01 00:00:05\n");
    REQUIRE(openCueSheet(backwards, {}, {}).status == DiscStatus::malformed);
}

TEST_CASE("A CUE track cannot extend past its FILE", "[disc-cue]")
{
    TestDirectory t;
    writeSized(t, "short.bin", 10 * 2048);
    auto cue = writeCue(t, "short.cue", "FILE short.bin BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:20\n");
    REQUIRE(openCueSheet(cue, {}, {}).status == DiscStatus::malformed);
}
