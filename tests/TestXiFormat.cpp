#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/extract/EmbeddedSamples.h"
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/tracker/TrackerDocument.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace sonicator;

namespace {
struct XiBytes
{
    std::vector<juce::uint8> v;
    void u8(int x) { v.push_back((juce::uint8) (x & 0xff)); }
    void u16le(int x) { u8(x); u8(x >> 8); }
    void u32le(juce::uint32 x) { for (int i = 0; i < 4; ++i) u8((int) ((x >> (8 * i)) & 0xff)); }
    void ascii(const juce::String& text, size_t width)
    {
        const auto start = v.size();
        auto utf = text.toRawUTF8();
        const int n = juce::jmin((int) width, (int) text.getNumBytesAsUTF8());
        v.insert(v.end(), utf, utf + n);
        while (v.size() < start + width) v.push_back(0);
    }
};
std::vector<juce::uint8> makeXi(const juce::String& name, bool sixteenBit, int relativeNote,
                                int frames, int loopStart, int loopLength, int loopMode,
                                bool createPcm = true)
{
    XiBytes b;
    b.ascii("Extended Instrument: ", 21);
    b.ascii(name, 22);
    b.u8(0x1A);
    b.ascii("FastTracker v2.00", 20);
    b.u16le(0x0102);
    for (int i = 0; i < 96; ++i) b.u8(0); // volume envelope points
    for (int i = 0; i < 96; ++i) b.u8(0); // panning envelope points
    for (int i = 0; i < 8; ++i) b.u8(0);  // counts and loop points
    for (int i = 0; i < 6; ++i) b.u8(0);  // types, vibrato
    b.u32le(0);                           // fadeout
    for (int i = 0; i < 22; ++i) b.u8(0); // reserved
    REQUIRE(b.v.size() == 298);
    const int bytesPerFrame = sixteenBit ? 2 : 1;
    b.u32le((juce::uint32) (frames * bytesPerFrame));
    b.u32le((juce::uint32) (loopStart * bytesPerFrame));
    b.u32le((juce::uint32) (loopLength * bytesPerFrame));
    b.u8(64);                                      // volume
    b.u8(0);                                       // finetune
    b.u8((sixteenBit ? 0x10 : 0) | (loopMode & 0x03));
    b.u8(128);                                     // panning
    b.u8(relativeNote & 0xff);                     // relative note, signed
    b.u8(0);                                       // reserved
    b.ascii(name, 22);
    REQUIRE(b.v.size() == 338);
    if (! createPcm) return b.v;
    int32_t accumulator = 0;
    for (int i = 0; i < frames; ++i)
    {
        const int32_t target = sixteenBit ? (i * 512 - 16384) : (i * 2 - 64);
        const int32_t delta = target - accumulator;
        accumulator = target;
        if (sixteenBit) { b.u8((int) ((uint16_t) delta & 0xff)); b.u8((int) (((uint16_t) delta >> 8) & 0xff)); }
        else b.u8((int) (int8_t) delta);
    }
    return b.v;
}
juce::File writeXi(TestDirectory& t, const juce::String& name, const std::vector<juce::uint8>& bytes)
{
    auto file = t.dir.getChildFile(name);
    file.replaceWithData(bytes.data(), bytes.size());
    return file;
}
} // namespace

TEST_CASE("XI 8-bit sample decodes delta PCM with its loop and root", "[xi][tracker]")
{
    TestDirectory t;
    const auto file = writeXi(t, "tone.xi", makeXi("Tone", false, 0, 64, 0, 32, 1));
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    CHECK(document->info().format == TrackerFormat::xi);
    CHECK(document->info().formatName == "XI");
    CHECK(document->info().title == "Tone");
    CHECK(document->info().sampleSlots == 1);
    CHECK(document->info().instruments == 1);
    REQUIRE(document->slots().size() == 1);
    const auto& slot = document->slots()[0];
    CHECK_FALSE(slot.empty);
    CHECK(slot.frames == 64);
    CHECK(slot.bitsPerSample == 8);
    CHECK(slot.sampleRate == 8363);
    CHECK(slot.rootKey == 60);
    CHECK(slot.looping);
    CHECK(slot.loopStart == 0);
    CHECK(slot.loopEnd == 32);
    CHECK_FALSE(slot.pingPong);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    for (int i = 0; i < 64; ++i)
        CHECK(audio->left[(size_t) i] == Catch::Approx((float) (i * 2 - 64) / 128.0f).margin(1.0e-6));
}

TEST_CASE("XI 16-bit sample decodes delta PCM with a ping-pong loop and relative note", "[xi][tracker]")
{
    TestDirectory t;
    const auto file = writeXi(t, "sixteen.xi", makeXi("Sixteen", true, 12, 64, 8, 32, 2));
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    const auto& slot = document->slots()[0];
    CHECK(slot.frames == 64);
    CHECK(slot.bitsPerSample == 16);
    CHECK(slot.sampleRate == 16726); // 8363 * 2^(12/12)
    CHECK(slot.rootKey == 48);       // the note that plays the 8363 Hz reference
    CHECK(slot.looping);
    CHECK(slot.pingPong);
    CHECK(slot.loopStart == 8);
    CHECK(slot.loopEnd == 40);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    for (int i = 0; i < 64; ++i)
        CHECK(audio->left[(size_t) i]
              == Catch::Approx((float) (i * 512 - 16384) / 32768.0f).margin(1.0e-6));
}

TEST_CASE("XI malformed and empty instruments are rejected with a reason", "[xi][tracker][malformed]")
{
    TestDirectory t;
    juce::String error;
    auto bytes = makeXi("Broken", false, 0, 64, 0, 32, 0);
    bytes.resize(bytes.size() - 20);
    auto truncated = writeXi(t, "truncated.xi", bytes);
    CHECK(TrackerDocument::open(truncated, error) == nullptr);
    CHECK(error.containsIgnoreCase("truncated"));

    auto empty = writeXi(t, "empty.xi", makeXi("Empty", false, 0, 0, 0, 0, 0, false));
    error.clear();
    CHECK(TrackerDocument::open(empty, error) == nullptr);
    CHECK(error.containsIgnoreCase("sample"));

    auto text = writeXi(t, "fake.xi", { 'h', 'e', 'l', 'l', 'o' });
    error.clear();
    CHECK(TrackerDocument::open(text, error) == nullptr);
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("XI opens as one instrument, not as a song, and extracts its PCM", "[xi][preview][extract]")
{
    TestDirectory t;
    const auto file = writeXi(t, "tone.xi", makeXi("Tone", false, 0, 64, 0, 32, 1));
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(file));
    CHECK(engine.isTrackerMode());
    CHECK(engine.isTrackerSampleSelected());
    CHECK(engine.selectedTrackerSampleId() == 0);
    CHECK_FALSE(engine.trackerHasSong());
    CHECK(engine.trackerInfo().format == TrackerFormat::xi);
    CHECK(engine.trackerSlots().size() == 1);
    CHECK(engine.length() == Catch::Approx(64.0 / 8363.0).margin(1.0e-6));
    CHECK_FALSE(engine.selectTrackerWholeSong());
    CHECK(engine.error().containsIgnoreCase("instrument"));

    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    auto* box = dynamic_cast<juce::ComboBox*>(preview.findChildWithID("trackerSample"));
    REQUIRE(box != nullptr);
    CHECK(box->getNumItems() == 1); // only the instrument slot, no "Whole song"
    CHECK(box->getItemText(0) == "Tone");
    CHECK(preview.trackerSampleCount() == 1);
    CHECK(preview.containerSourceList().getListBoxModel()->getNumRows() == 1);
    CHECK(preview.waveformLength() == Catch::Approx(64.0 / 8363.0).margin(1.0e-6));

    EmbeddedSampleSet set;
    juce::String error;
    REQUIRE(set.open(file, error));
    REQUIRE(set.entries().size() == 1);
    CHECK(set.entries()[0].name == "Tone");
    auto audio = set.read(0, error);
    REQUIRE(audio != nullptr);
    CHECK(audio->frames == 64);
    CHECK(set.playbackRatio(0) == Catch::Approx(1.0).margin(1.0e-9));
    CHECK(isEmbeddedSampleSource(file));
    CHECK_FALSE(isTrackerModuleSource(file));
    const auto sources = embeddedSources({ file });
    REQUIRE(sources.size() == 1);

    REQUIRE(FileScanner::isSupportedFormat(file));
    CHECK(FileScanner::isMetadataOnlyFormat(file));
    const auto record = analyzeMetadataAsset(file, {});
    CHECK(record.error.isEmpty());
    CHECK(record.format == "XI");
    CHECK(record.compatibility == AssetCompatibility::supported);
    CHECK_FALSE(record.fileHash.isEmpty());
}

TEST_CASE("XI relative-note extraction keeps the natural sample rate", "[xi][extract]")
{
    TestDirectory t;
    const auto file = writeXi(t, "octave.xi", makeXi("Octave", false, 12, 64, 0, 0, 0));
    EmbeddedSampleSet set;
    juce::String error;
    REQUIRE(set.open(file, error));
    REQUIRE(set.entries().size() == 1);
    // The document rate is the note-60 rate (16726 Hz); the root is note 48, so extraction must
    // resample by 2^-1 to recover the 8363 Hz reference rate.
    CHECK(set.playbackRatio(0) == Catch::Approx(0.5).margin(1.0e-9));
}

// Opt-in corpus check. SONICATOR_XI_DIR points at the read-only library; the summary and the
// rejection reasons are printed and recorded in docs/XI.md.
TEST_CASE("Real XI corpus opens read-only", "[.external][xi-corpus]")
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("SONICATOR_XI_DIR", {});
    REQUIRE(directory.isNotEmpty());
    const juce::File root(directory);
    REQUIRE(root.isDirectory());
    std::vector<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.xi;*.XI",
                                                           juce::File::findFiles))
        files.push_back(entry.getFile());
    std::sort(files.begin(), files.end(), [] (const auto& a, const auto& b)
              { return a.getFullPathName() < b.getFullPathName(); });
    int opened = 0, rejected = 0, samples = 0;
    int64_t frames = 0;
    juce::StringArray reasons;
    for (const auto& file : files)
    {
        juce::String error;
        auto document = TrackerDocument::open(file, error);
        if (document == nullptr) { ++rejected; reasons.add(file.getFileName() + ": " + error); continue; }
        ++opened;
        for (const auto& slot : document->slots())
        {
            if (slot.empty || slot.error.isNotEmpty()) continue;
            juce::String sampleError;
            if (auto audio = document->sampleAudio(slot.slot, sampleError))
            {
                ++samples;
                frames += audio->frames;
            }
            else { ++rejected; reasons.add(file.getFileName() + ": " + sampleError); }
        }
    }
    std::cout << "XI corpus: files=" << files.size() << " opened=" << opened
              << " samples=" << samples << " frames=" << frames << " rejected=" << rejected << "\n";
    for (int i = 0; i < juce::jmin(30, reasons.size()); ++i) std::cout << "  " << reasons[i] << "\n";
    INFO("files=" << files.size() << " opened=" << opened << " samples=" << samples
         << " frames=" << frames << " rejected=" << rejected);
    INFO(reasons.joinIntoString("\n"));
    REQUIRE_FALSE(files.empty());
    CHECK(opened > 0);
    CHECK(samples > 0);
}
