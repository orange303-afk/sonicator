#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/audio/SampleVoicePlayer.h"
#include "core/export/AudioExporter.h"
#include "core/legacy/LegacyFormatProbe.h"
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/tracker/TrackerDocument.h"
#include "core/tracker/TrackerSongRenderer.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <algorithm>
#include <cmath>
#include <vector>
using namespace sonicator;
namespace {
struct Blob
{
    std::vector<juce::uint8> v;
    void u8(int x) { v.push_back((juce::uint8) x); }
    void u16le(int x) { u8(x & 0xff); u8((x >> 8) & 0xff); }
    void u16be(int x) { u8((x >> 8) & 0xff); u8(x & 0xff); }
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
juce::File write(TestDirectory& t, const juce::String& name, const std::vector<juce::uint8>& bytes)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    auto out = file.createOutputStream();
    if (! bytes.empty()) out->write(bytes.data(), (int) bytes.size());
    return file;
}
std::vector<juce::uint8> makeMod31(bool withNote, int channels = 4, const char* tag = "M.K.")
{
    Blob b;
    b.ascii("Test MOD", 20);
    for (int i = 0; i < 31; ++i)
    {
        if (i == 0)
        {
            b.ascii("sine", 22);
            b.u16be(32); // 64 bytes
            b.u8(0); b.u8(64);
            b.u16be(0); b.u16be(32); // loop the whole sample
        }
        else
        {
            b.ascii("", 22);
            b.u16be(0); b.u8(0); b.u8(0); b.u16be(0); b.u16be(1);
        }
    }
    b.u8(1); b.u8(0);
    for (int i = 0; i < 128; ++i) b.u8(0);
    b.ascii(tag, 4);
    std::vector<juce::uint8> pattern((size_t) channels * 64 * 4, 0);
    if (withNote)
    {
        pattern[0] = 0x01; // period high nibble 1, sample high nibble 0
        pattern[1] = 0xAC; // period 428 (C-4)
        pattern[2] = 0x1F; // sample 1 (low nibble), effect F
        pattern[3] = 0x06; // speed 6
    }
    b.v.insert(b.v.end(), pattern.begin(), pattern.end());
    for (int i = 0; i < 64; ++i) b.u8((juce::uint8) (int8_t) (i * 2 - 64));
    return b.v;
}
std::vector<juce::uint8> makeMod15()
{
    Blob b;
    b.ascii("Fifteen", 20);
    for (int i = 0; i < 15; ++i)
    {
        if (i == 0)
        {
            b.ascii("fifteen", 22);
            b.u16be(16); b.u8(0); b.u8(64); b.u16be(0); b.u16be(16);
        }
        else
        {
            b.ascii("", 22);
            b.u16be(0); b.u8(0); b.u8(0); b.u16be(0); b.u16be(1);
        }
    }
    b.u8(1); b.u8(0);
    for (int i = 0; i < 128; ++i) b.u8(0);
    for (int i = 0; i < 1024; ++i) b.u8(0);
    for (int i = 0; i < 32; ++i) b.u8((juce::uint8) (int8_t) (i * 2 - 32));
    return b.v;
}
std::vector<juce::uint8> makeXm(bool sixteenBit = false)
{
    Blob b;
    b.ascii("Extended Module: ", 17);
    b.ascii("Test XM", 20);
    b.u8(0x1A);
    b.ascii("Sonicator", 20);
    b.u16le(0x0104);
    b.u32le(21);
    b.u16le(1); b.u16le(0); b.u16le(4); b.u16le(1); b.u16le(1);
    b.u16le(0); b.u16le(6); b.u16le(125);
    b.u8(0); // order
    // Pattern: the u32 is the 9-byte header size; packed data follows it.
    b.u32le(9); b.u8(0); b.u16le(64); b.u16le(0);
    // Instrument: a 263-byte fixed header, then one 40-byte sample header, then sample data.
    b.u32le(263);
    b.ascii("inst", 22);
    b.u8(0);
    b.u16le(1);
    b.u32le(40);
    for (int i = 0; i < 96; ++i) b.u8(0); // note map
    for (int i = 0; i < 48; ++i) b.u8(0); // volume envelope
    for (int i = 0; i < 48; ++i) b.u8(0); // panning envelope
    for (int i = 0; i < 8; ++i) b.u8(0);  // counts and loop points
    b.u8(0); b.u8(0);                     // envelope types
    b.u8(0); b.u8(0); b.u8(0); b.u8(0);   // vibrato
    b.u16le(0);                            // fadeout
    for (int i = 0; i < 22; ++i) b.u8(0); // reserved
    const int frames = 64;
    const int bytesPerFrame = sixteenBit ? 2 : 1;
    b.u32le(frames * bytesPerFrame);
    b.u32le(0);
    b.u32le(frames * bytesPerFrame);
    b.u8(64); b.u8(0); b.u8(sixteenBit ? 0x11 : 0x01); b.u8(128); b.u8(0); b.u8(0);
    b.ascii("delta", 22);
    int32_t accumulator = 0;
    for (int i = 0; i < frames; ++i)
    {
        const int32_t target = sixteenBit ? (i * 512 - 16384) : (i * 2 - 64);
        const int32_t delta = target - accumulator;
        accumulator = target;
        if (sixteenBit)
        {
            b.u8((int) ((uint16_t) delta & 0xff));
            b.u8((int) (((uint16_t) delta >> 8) & 0xff));
        }
        else b.u8((juce::uint8) (int8_t) delta);
    }
    return b.v;
}
std::vector<juce::uint8> makeS3m(bool sixteenBit = false)
{
    Blob b;
    b.ascii("Test S3M", 28);
    b.u8(0x1A); b.u8(0x10); b.u8(0); b.u8(0);
    b.u16le(1); b.u16le(1); b.u16le(1); b.u16le(0); b.u16le(0x1320); b.u16le(2);
    b.ascii("SCRM", 4);
    b.u8(64); b.u8(6); b.u8(125); b.u8(0xB0); b.u8(0); b.u8(0);
    for (int i = 0; i < 8; ++i) b.u8(0);
    while (b.v.size() < 96) b.u8(0); // the pointer/order tables start at byte 96
    b.u8(0);        // order
    b.u16le(7);     // instrument paragraph -> 112
    b.u16le(20);    // pattern paragraph -> 320
    while (b.v.size() < 112) b.u8(0);
    b.u8(1);
    b.ascii("", 12);
    b.u8(0);
    b.u16le(12); // sample paragraph -> 192
    b.u32le(64);
    b.u32le(0); b.u32le(64);
    b.u8(64); b.u8(0); b.u8(0);
    b.u8(sixteenBit ? 0x05 : 0x01); // loop + optional 16-bit
    b.u32le(8363);
    for (int i = 0; i < 12; ++i) b.u8(0);
    b.ascii("unsigned", 28);
    b.ascii("SCRS", 4);
    while (b.v.size() < 192) b.u8(0);
    if (sixteenBit)
        for (int i = 0; i < 64; ++i) b.u16le(32768 + i * 100);
    else
        for (int i = 0; i < 64; ++i) b.u8(128 + (i < 32 ? i : 63 - i));
    while (b.v.size() < 320) b.u8(0);
    for (int i = 0; i < 64; ++i) b.u8(0);
    return b.v;
}
} // namespace

TEST_CASE("Tracker probe recognises MOD/XM/S3M by content and rejects other binaries",
          "[tracker][probe]")
{
    TestDirectory t;
    const auto mod = write(t, "song.MOD", makeMod31(true));
    const auto mod15 = write(t, "old.mod", makeMod15());
    const auto xm = write(t, "song.xm", makeXm());
    const auto s3m = write(t, "song.s3m", makeS3m());
    const auto text = write(t, "fake.xm", {'j', 'u', 's', 't', ' ', 't', 'e', 'x', 't'});
    const auto empty = write(t, "empty.mod", {});

    const auto modProbe = probeLegacyTracker(mod);
    REQUIRE(modProbe.recognised);
    REQUIRE(modProbe.format == TrackerFormat::mod);
    REQUIRE(modProbe.channels == 4);
    REQUIRE(modProbe.sampleSlots == 31);
    REQUIRE(modProbe.songPlayback);
    REQUIRE(modProbe.sampleExtraction);

    const auto mod15Probe = probeLegacyTracker(mod15);
    REQUIRE(mod15Probe.recognised);
    REQUIRE(mod15Probe.format == TrackerFormat::mod);
    REQUIRE(mod15Probe.sampleSlots == 15);

    REQUIRE(probeLegacyTracker(xm).format == TrackerFormat::xm);
    REQUIRE(probeLegacyTracker(s3m).format == TrackerFormat::s3m);
    REQUIRE_FALSE(probeLegacyTracker(text).recognised);
    REQUIRE_FALSE(probeLegacyTracker(empty).recognised);

    REQUIRE(FileScanner::isSupportedFormat(mod));
    REQUIRE(FileScanner::isMetadataOnlyFormat(mod));
    REQUIRE(FileScanner::isSupportedFormat(xm));
    // A candidate extension is not a claim of support: the content probe rejects it and the
    // catalog analyzer records an error instead of an audio row.
    REQUIRE(FileScanner::isSupportedFormat(text));
}

TEST_CASE("MOD samples decode signed 8-bit PCM with loops and a reference rate",
          "[tracker][mod]")
{
    TestDirectory t;
    const auto file = write(t, "song.mod", makeMod31(true));
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    REQUIRE(document->info().format == TrackerFormat::mod);
    REQUIRE(document->info().sampleSlots == 31);
    REQUIRE(document->info().channels == 4);
    REQUIRE(document->info().title == "Test MOD");
    const auto& slot = document->slots()[0];
    REQUIRE_FALSE(slot.empty);
    REQUIRE(slot.frames == 64);
    REQUIRE(slot.bitsPerSample == 8);
    REQUIRE(slot.sampleRate == 8363);
    REQUIRE(slot.looping);
    REQUIRE(slot.loopStart == 0);
    REQUIRE(slot.loopEnd == 64);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    REQUIRE(audio->frames == 64);
    REQUIRE(audio->left[0] == Catch::Approx(-64.0f / 128.0f));
    REQUIRE(audio->left[63] == Catch::Approx(62.0f / 128.0f));
}

TEST_CASE("Multichannel MOD patterns are skipped using the declared channel count",
          "[tracker][mod]")
{
    TestDirectory t;
    const auto file = write(t, "eight.mod", makeMod31(true, 8, "FLT8"));
    const auto probe = probeLegacyTracker(file);
    REQUIRE(probe.recognised);
    REQUIRE(probe.channels == 8);
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    REQUIRE(document->info().channels == 8);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    REQUIRE(audio->frames == 64);
    // The PCM must come from the sample block, not from the eight-channel pattern data.
    REQUIRE(audio->left[0] == Catch::Approx(-64.0f / 128.0f));
    REQUIRE(audio->left[63] == Catch::Approx(62.0f / 128.0f));
}

TEST_CASE("XM delta decoding and instrument-relative tuning match the stored source",
          "[tracker][xm]")
{
    TestDirectory t;
    const auto file = write(t, "song.xm", makeXm());
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    REQUIRE(document->info().format == TrackerFormat::xm);
    REQUIRE(document->info().sampleSlots == 1);
    REQUIRE(document->info().channels == 4);
    const auto& slot = document->slots()[0];
    REQUIRE(slot.frames == 64);
    REQUIRE(slot.bitsPerSample == 8);
    REQUIRE(slot.sampleRate == 8363);
    REQUIRE(slot.looping);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    for (int i = 0; i < 64; ++i)
        REQUIRE(audio->left[(size_t) i] == Catch::Approx((float) (i * 2 - 64) / 128.0f).margin(1.0e-6));
}

TEST_CASE("XM 16-bit delta samples decode with frame-based loop bounds", "[tracker][xm]")
{
    TestDirectory t;
    const auto file = write(t, "sixteen.xm", makeXm(true));
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    const auto& slot = document->slots()[0];
    REQUIRE(slot.frames == 64);
    REQUIRE(slot.bitsPerSample == 16);
    REQUIRE(slot.looping);
    REQUIRE(slot.loopEnd == 64);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    for (int i = 0; i < 64; ++i)
        REQUIRE(audio->left[(size_t) i]
                == Catch::Approx((float) (i * 512 - 16384) / 32768.0f).margin(1.0e-6));
}

TEST_CASE("S3M 16-bit unsigned samples decode with frame lengths", "[tracker][s3m]")
{
    TestDirectory t;
    const auto file = write(t, "sixteen.s3m", makeS3m(true));
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    const auto& slot = document->slots()[0];
    REQUIRE(slot.frames == 64);
    REQUIRE(slot.bitsPerSample == 16);
    REQUIRE(slot.looping);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    REQUIRE(audio->left[0] == Catch::Approx(0.0f));
    REQUIRE(audio->left[10] == Catch::Approx(1000.0f / 32768.0f).margin(1.0e-6));
}

TEST_CASE("S3M unsigned samples decode with C2 speed and loop bounds", "[tracker][s3m]")
{
    TestDirectory t;
    const auto file = write(t, "song.s3m", makeS3m());
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    REQUIRE(document->info().format == TrackerFormat::s3m);
    const auto& slot = document->slots()[0];
    REQUIRE(slot.frames == 64);
    REQUIRE(slot.sampleRate == 8363);
    REQUIRE(slot.looping);
    auto audio = document->sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    REQUIRE(audio->left[0] == Catch::Approx(0.0f));
    REQUIRE(audio->left[8] == Catch::Approx(8.0f / 128.0f));
}

TEST_CASE("A renamed S3M under a .mod name is opened by content, not extension",
          "[tracker][dispatch]")
{
    TestDirectory t;
    const auto file = write(t, "renamed.mod", makeS3m());
    const auto probe = probeLegacyTracker(file);
    REQUIRE(probe.recognised);
    REQUIRE(probe.format == TrackerFormat::s3m);
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    REQUIRE(document != nullptr);
    REQUIRE(document->info().format == TrackerFormat::s3m);
}

TEST_CASE("Truncated tracker payloads are rejected instead of decoded as silence",
          "[tracker][malformed]")
{
    TestDirectory t;
    auto bytes = makeMod31(true);
    bytes.resize(bytes.size() - 20); // sample data no longer reaches the declared length
    const auto file = write(t, "truncated.mod", bytes);
    REQUIRE_FALSE(probeLegacyTracker(file).recognised);
    juce::String error;
    REQUIRE(TrackerDocument::open(file, error) == nullptr);

    const auto text = write(t, "notes.mod", {'h', 'e', 'l', 'l', 'o'});
    REQUIRE(TrackerDocument::open(text, error) == nullptr);
    REQUIRE_FALSE(error.isEmpty());
}

TEST_CASE("Catalog analysis records the detected tracker format and its errors",
          "[tracker][catalog]")
{
    TestDirectory t;
    const auto mod = write(t, "song.mod", makeMod31(true));
    const auto renamed = write(t, "renamed.mod", makeS3m());
    const auto fake = write(t, "fake.mod", {'h', 'e', 'l', 'l', 'o'});
    const auto record = analyzeMetadataAsset(mod, {});
    REQUIRE(record.error.isEmpty());
    REQUIRE(record.format == "MOD");
    REQUIRE(record.numChannels == 4);
    REQUIRE_FALSE(record.fileHash.isEmpty());
    const auto renamedRecord = analyzeMetadataAsset(renamed, {});
    REQUIRE(renamedRecord.format == "S3M");
    const auto fakeRecord = analyzeMetadataAsset(fake, {});
    REQUIRE_FALSE(fakeRecord.error.isEmpty());
}

TEST_CASE("Tracker song rendering produces audio, seeks and stops", "[tracker][renderer]")
{
    TestDirectory t;
    const auto file = write(t, "song.mod", makeMod31(true));
    TrackerSongRenderer renderer;
    juce::String error;
    REQUIRE(renderer.load(file, 44100.0, error));
    REQUIRE(renderer.hasModule());
    REQUIRE(renderer.length() > 0.0);
    renderer.prepare(44100.0);
    renderer.play();
    juce::AudioBuffer<float> buffer(2, 512);
    float peak = 0;
    for (int i = 0; i < 16; ++i)
    {
        buffer.clear();
        renderer.render(buffer, 0, 512);
        peak = juce::jmax(peak, buffer.getMagnitude(0, 0, 512));
    }
    REQUIRE(peak > 0.001f);
    REQUIRE(peak <= 1.01f); // interleaved int16 must always be normalized on its way into the ring
    REQUIRE(renderer.position() > 0.0);
    renderer.seek(0.0);
    REQUIRE(renderer.position() < 0.2);
    renderer.stop();
    REQUIRE(renderer.position() == Catch::Approx(0.0));
    REQUIRE_FALSE(renderer.isPlaying());
}

TEST_CASE("AudioEngine switches between whole tracker song and its selected sample",
          "[tracker][engine]")
{
    TestDirectory t;
    const auto file = write(t, "song.mod", makeMod31(true));
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(file));
    REQUIRE(engine.isTrackerMode());
    REQUIRE(engine.trackerSlots().size() == 31);
    REQUIRE(engine.trackerInfo().format == TrackerFormat::mod);
    REQUIRE_FALSE(engine.isTrackerSampleSelected());
    engine.preparePreviewBlock(512);

    engine.play();
    REQUIRE(engine.isPlaying());
    juce::AudioBuffer<float> buffer(2, 512);
    float songPeak = 0;
    for (int i = 0; i < 12; ++i)
    {
        buffer.clear();
        engine.renderPreviewBlock(buffer, 512);
        songPeak = juce::jmax(songPeak, buffer.getMagnitude(0, 0, 512));
    }
    REQUIRE(songPeak > 0.001f);

    // Pause is immediate: the buffered ring is not audible and is not consumed.
    engine.pause();
    buffer.clear();
    engine.renderPreviewBlock(buffer, 512);
    REQUIRE(buffer.getMagnitude(0, 0, 512) == 0.0f);
    engine.play();
    REQUIRE(engine.isPlaying());

    REQUIRE(engine.selectTrackerSample(0));
    REQUIRE(engine.isTrackerSampleSelected());
    REQUIRE_FALSE(engine.isPlaying()); // selecting a source stops the song
    REQUIRE(engine.trackerSampleAudio() != nullptr);
    REQUIRE(engine.trackerSampleAudio()->frames == 64);
    REQUIRE(engine.length() == Catch::Approx(64.0 / 8363.0).margin(1.0e-6));

    engine.play();
    buffer.clear();
    engine.renderPreviewBlock(buffer, 512);
    REQUIRE(buffer.getMagnitude(0, 0, 512) > 0.0f);

    REQUIRE(engine.selectTrackerWholeSong());
    REQUIRE_FALSE(engine.isTrackerSampleSelected());
    engine.play();
    for (int i = 0; i < 12; ++i)
    {
        buffer.clear();
        engine.renderPreviewBlock(buffer, 512);
    }
    REQUIRE(engine.isPlaying());
    engine.stop();
    REQUIRE(engine.position() == Catch::Approx(0.0));
    // Stop also rewinds the composition while a sample is the active source.
    REQUIRE(engine.selectTrackerSample(0));
    engine.play();
    engine.stop();
    REQUIRE(engine.position() == Catch::Approx(0.0));
    REQUIRE(engine.selectTrackerWholeSong());
    REQUIRE(engine.position() == Catch::Approx(0.0));
}

TEST_CASE("Selected tracker sample plays chromatically over MIDI with a two-second release",
          "[tracker][midi]")
{
    TestDirectory t;
    const auto file = write(t, "song.mod", makeMod31(true));
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(file));
    REQUIRE(engine.selectTrackerSample(0));
    REQUIRE(engine.setMidiPreview(true));
    engine.preparePreviewBlock(512);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 60, 1.0f));
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    engine.renderPreviewBlock(buffer, 64);
    REQUIRE(buffer.getMagnitude(0, 0, 64) > 0.0f);
    REQUIRE(engine.trackerSampleActiveVoices() == 1);
    engine.midiCollector().push(juce::MidiMessage::noteOff(1, 60));
    for (int i = 0; i < 20; ++i)
    {
        buffer.clear();
        engine.renderPreviewBlock(buffer, 512);
    }
    REQUIRE(engine.trackerSampleActiveVoices() == 0); // the 64-frame source ends
}

TEST_CASE("Selected tracker sample exports the exact decoded PCM as WAV",
          "[tracker][export]")
{
    TestDirectory t;
    const auto file = write(t, "song.mod", makeMod31(true));
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(file, false));
    REQUIRE(preview.trackerSampleCount() == 31);
    REQUIRE(preview.trackerWholeSongSelected());
    REQUIRE(preview.selectTrackerSlot(0));
    REQUIRE(preview.selectedTrackerSampleId() == 0);
    const double seconds = 64.0 / 8363.0;
    preview.selectRange(0.0, seconds);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    const auto exported = preview.preparedSelection();
    REQUIRE(exported.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(exported));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->numChannels == 1);
    REQUIRE(reader->bitsPerSample == 16);
    REQUIRE(reader->sampleRate == Catch::Approx(8363.0));
    REQUIRE(reader->lengthInSamples == 64);
    juce::AudioBuffer<float> decoded(1, 64);
    REQUIRE(reader->read(&decoded, 0, 64, 0, true, true));
    REQUIRE(decoded.getSample(0, 0) == Catch::Approx(-64.0f / 128.0f).margin(2.0e-4));
    REQUIRE(decoded.getSample(0, 63) == Catch::Approx(62.0f / 128.0f).margin(2.0e-4));
}

TEST_CASE("Whole-song selection hides sample export and never shows the first sample waveform",
          "[tracker][ui]")
{
    TestDirectory t;
    const auto file = write(t, "song.mod", makeMod31(true));
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    preview.setSize(900, 200);
    REQUIRE(preview.loadFile(file, false));
    auto* trackerBox = dynamic_cast<juce::ComboBox*>(preview.findChildWithID("trackerSample"));
    REQUIRE(trackerBox != nullptr);
    REQUIRE(trackerBox->isVisible());
    REQUIRE(trackerBox->getWidth() > 0); // layout runs when the selector appears
    auto* dragButton = dynamic_cast<juce::Button*>(preview.findChildWithID("dragTrackerSample"));
    REQUIRE(dragButton != nullptr);
    REQUIRE_FALSE(dragButton->isVisible());
    auto* progress = dynamic_cast<SongProgress*>(preview.findChildWithID("songProgress"));
    REQUIRE(progress != nullptr);
    REQUIRE(progress->isVisible());
    REQUIRE(progress->length() == Catch::Approx(engine.length()).margin(1.0e-6));
    REQUIRE(SongProgress::formatTime(0.0) == "00:00:000");
    REQUIRE(SongProgress::formatTime(65.5) == "01:05:500");
    REQUIRE(SongProgress::formatTime(3725.123) == "62:05:123");
    REQUIRE(preview.trackerWholeSongSelected());
    REQUIRE(preview.waveformLength() == Catch::Approx(0.0).margin(1.0e-9)); // no fake overview
    REQUIRE(preview.selectTrackerSlot(0));
    REQUIRE(dragButton->isVisible());
    REQUIRE_FALSE(progress->isVisible()); // the waveform owns the area for a source sample
    REQUIRE_FALSE(preview.trackerWholeSongSelected());
    REQUIRE(preview.waveformLength() == Catch::Approx(64.0 / 8363.0).margin(1.0e-6));
    preview.clearSelection();
    preview.selectTrackerWholeSong();
    REQUIRE(preview.trackerWholeSongSelected());
    REQUIRE(progress->isVisible());
    REQUIRE(progress->length() == Catch::Approx(engine.length()).margin(1.0e-6));
    REQUIRE(preview.waveformLength() == Catch::Approx(0.0).margin(1.0e-9));
}

TEST_CASE("SampleVoicePlayer keeps a two-second release and refuses a 129th voice",
          "[tracker][voices]")
{
    SampleAudio audio;
    audio.sampleRate = 44100;
    audio.channels = 1;
    audio.frames = 44100 * 5; // long enough that the source end cannot mask the release
    audio.left.assign((size_t) audio.frames, 0.5f);
    auto source = std::make_shared<const SampleAudio>(audio);
    SampleVoicePlayer player;
    player.setSource(source, false, 0, 0, false);
    player.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    buffer.clear();
    player.render(buffer, 0, 512, midi);
    REQUIRE(player.activeVoices() == 1);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    for (int i = 0; i < 43; ++i) // ~0.5 s: still inside the 2 s release
    {
        buffer.clear();
        player.render(buffer, 0, 512, off);
    }
    REQUIRE(player.activeVoices() == 1);
    for (int i = 0; i < 120; ++i) // ~1.89 s after NoteOff
    {
        buffer.clear();
        player.render(buffer, 0, 512, off);
    }
    REQUIRE(player.activeVoices() == 1);
    for (int i = 0; i < 10; ++i) // past the 2 s release
    {
        buffer.clear();
        player.render(buffer, 0, 512, off);
    }
    REQUIRE(player.activeVoices() == 0);

    juce::MidiBuffer storm;
    for (int i = 0; i < 129; ++i)
        storm.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    buffer.clear();
    player.render(buffer, 0, 512, storm);
    REQUIRE(player.activeVoices() == 128);
}

// Opt-in real-corpus check. SONICATOR_TRACKER_DIR points at a read-only collection; only the
// first 24 modules are opened so the run stays bounded. No file is ever written.
TEST_CASE("Real tracker corpus opens read-only and decodes its samples",
          "[.external][tracker-corpus]")
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("SONICATOR_TRACKER_DIR", {});
    REQUIRE(directory.isNotEmpty());
    const juce::File root(directory);
    REQUIRE(root.isDirectory());
    int opened = 0, rejected = 0, samples = 0;
    juce::StringArray rejectedNames;
    std::vector<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.mod;*.xm;*.s3m",
                                                           juce::File::findFiles))
        files.push_back(entry.getFile());
    std::sort(files.begin(), files.end(), [] (const auto& a, const auto& b)
              { return a.getFullPathName() < b.getFullPathName(); });
    for (const auto& file : files)
    {
        if (opened + rejected >= 48) break;
        const auto before = FileScanner::computeFileHash(file, {});
        juce::String error;
        auto document = TrackerDocument::open(file, error);
        if (document == nullptr) { ++rejected; rejectedNames.add(file.getFileName()); continue; }
        ++opened;
        for (const auto& slot : document->slots())
        {
            if (slot.empty || slot.error.isNotEmpty()) continue;
            juce::String sampleError;
            if (document->sampleAudio(slot.slot, sampleError) != nullptr) ++samples;
        }
        REQUIRE(FileScanner::computeFileHash(file, {}) == before);
    }
    INFO("opened=" << opened << " rejected=" << rejected << " samples=" << samples
         << " rejectedNames=" << rejectedNames.joinIntoString(","));
    REQUIRE(opened > 0);
    REQUIRE(samples > 0);
    // Playback is a separate capability: render a short prefix of the first modules and require
    // audible output from the ones that carry notes, without assuming every module is loud.
    int audible = 0, rendered = 0;
    juce::AudioBuffer<float> buffer(2, 512);
    for (const auto& file : files)
    {
        if (rendered >= 8) break;
        TrackerSongRenderer renderer;
        juce::String error;
        if (! renderer.load(file, 44100.0, error)) continue;
        ++rendered;
        renderer.prepare(44100.0);
        renderer.play();
        float peak = 0;
        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            renderer.render(buffer, 0, 512);
            peak = juce::jmax(peak, buffer.getMagnitude(0, 0, 512));
        }
        if (peak > 0.0005f) ++audible;
    }
    INFO("rendered=" << rendered << " audible=" << audible);
    REQUIRE(rendered > 0);
    REQUIRE(audible > 0);
}

TEST_CASE("Audition honours tracker loop points for forward and ping-pong loops",
          "[tracker][audition]")
{
    SampleAudio audio;
    audio.sampleRate = 1000;
    audio.channels = 1;
    audio.frames = 100;
    audio.left.assign(100, 0.1f);
    auto source = std::make_shared<const SampleAudio>(audio);
    SampleAudition audition;
    audition.setAudio(source);
    audition.prepare(1000);
    audition.setLoopRange(20, 40, false);
    audition.play(true);
    juce::AudioBuffer<float> buffer(2, 400);
    buffer.clear();
    audition.render(buffer, 0, 400);
    REQUIRE(audition.isPlaying());
    REQUIRE(audition.position() >= 0.019);
    REQUIRE(audition.position() <= 0.041);

    audition.stop();
    audition.setLoopRange(20, 40, true);
    audition.play(true);
    buffer.clear();
    audition.render(buffer, 0, 400);
    REQUIRE(audition.isPlaying());
    REQUIRE(audition.position() >= 0.019);
    REQUIRE(audition.position() <= 0.041);

    // A non-zero loop start must play the pre-loop attack once, not jump into (or past) the loop.
    SampleAudio gated;
    gated.sampleRate = 1000;
    gated.channels = 1;
    gated.frames = 100;
    gated.left.assign(100, 0.5f);
    for (int i = 0; i < 20; ++i) gated.left[(size_t) i] = 0.0f;
    auto gatedSource = std::make_shared<const SampleAudio>(gated);
    audition.setAudio(gatedSource); // also resets the loop range to the whole source
    audition.setLoopRange(20, 40, false);
    audition.play(true);
    buffer.clear();
    audition.render(buffer, 0, 30);
    REQUIRE(buffer.getSample(0, 5) == 0.0f);
    REQUIRE(buffer.getSample(0, 25) > 0.0f);
}

TEST_CASE("Tracker song output clears the callback buffer before mixing", "[tracker][realtime]")
{
    TestDirectory t;
    const auto file = write(t, "song.mod", makeMod31(true));
    TrackerSongRenderer renderer;
    juce::String error;
    REQUIRE(renderer.load(file, 44100.0, error));
    juce::AudioTransportSource transport;
    MidiSampler sampler;
    Sf2KitSampler kit;
    Rex2SlicePlayer rex;
    SampleAudition audition;
    ExternalKitSampler external;
    SampleVoicePlayer voices;
    PreviewOutput output(transport, sampler, kit, rex, audition, external, renderer, voices);
    output.trackerSongMode = true;
    output.prepareToPlay(128, 44100);
    juce::AudioBuffer<float> buffer(2, 128);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 128; ++i) buffer.setSample(ch, i, 0.25f);
    output.getNextAudioBlock({ &buffer, 0, 128 });
    // The renderer is paused, so the whole song path must leave silence rather than the stale
    // contents of the device buffer it was handed.
    REQUIRE(buffer.getMagnitude(0, 128) == 0.0f);
}

TEST_CASE("Selector steps with arrow keys and clamps at the ends", "[tracker][ui][keys]")
{
    StepComboBox box;
    box.addItem("one", 1);
    box.addItem("two", 2);
    box.addItem("three", 3);
    box.setSelectedId(2, juce::dontSendNotification);
    REQUIRE(box.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    REQUIRE(box.getSelectedId() == 3);
    REQUIRE(box.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    REQUIRE(box.getSelectedId() == 3);
    REQUIRE(box.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    REQUIRE(box.getSelectedId() == 2);
    box.step(-1);
    REQUIRE(box.getSelectedId() == 1);
}

// Opt-in real-time diagnostic: render a real module at device pace and count ring underruns.
TEST_CASE("Real tracker file plays without ring underruns at device pace",
          "[.external][tracker-live]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_TRACKER_FILE", {});
    REQUIRE(path.isNotEmpty());
    TrackerSongRenderer renderer;
    juce::String error;
    REQUIRE(renderer.load(juce::File(path), 44100.0, error));
    renderer.prepare(44100.0);
    renderer.play();
    juce::AudioBuffer<float> buffer(2, 512);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    int silentBlocks = 0;
    float peak = 0;
    for (int i = 0; i < 400; ++i)
    {
        buffer.clear();
        renderer.render(buffer, 0, 512);
        const auto magnitude = buffer.getMagnitude(0, 512);
        peak = juce::jmax(peak, magnitude);
        if (magnitude == 0.0f) ++silentBlocks;
        const double target = start + (i + 1) * 512.0 / 44100.0 * 1000.0;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (target > now) juce::Thread::sleep((int) std::ceil(target - now));
    }
    INFO("underruns=" << renderer.underruns() << " silentBlocks=" << silentBlocks
         << " peak=" << peak << " position=" << renderer.position()
         << " length=" << renderer.length());
    REQUIRE(renderer.underruns() == 0);
    // A raw int16 sample accidentally stored as a float would push this far above 1.
    REQUIRE(peak <= 1.01f);
}
