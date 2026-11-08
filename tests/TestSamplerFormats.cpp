#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/export/AudioExporter.h"
#include "core/sampler/ExternalKitSampler.h"
#include "core/sampler/FileInstrument.h"
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/database/SampleRecord.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <zlib.h>
#include <cmath>
#include <vector>
using namespace sonicator;
namespace {
struct Bytes
{
    std::vector<juce::uint8> v;
    void u8(int x) { v.push_back((juce::uint8) x); }
    void u16(int x) { u8(x & 0xff); u8((x >> 8) & 0xff); }
    void u32(juce::uint32 x) { for (int i = 0; i < 4; ++i) u8((int) ((x >> (8 * i)) & 0xff)); }
    void i32(int x) { u32((juce::uint32) x); }
    void name64(const juce::String& s)
    {
        const auto start = v.size();
        auto utf = s.toRawUTF8();
        const int n = juce::jmin(63, (int) s.getNumBytesAsUTF8());
        v.insert(v.end(), utf, utf + n);
        while (v.size() < start + 64) v.push_back(0);
    }
};
void pad(Bytes& b, size_t size) { while (b.v.size() < size) b.v.push_back(0); }
void writeBlock(Bytes& file, int marker, int index, const juce::String& name, const Bytes& payload)
{
    Bytes header;
    header.u16(0x0101);
    header.u8(0);
    header.u8(marker);
    header.u16((int) payload.v.size());
    header.u16(0);
    header.u32((juce::uint32) index);
    header.u32(0);
    header.v.insert(header.v.end(), { 'T', 'B', 'O', 'S' });
    header.name64(name);
    REQUIRE(header.v.size() == 84);
    file.v.insert(file.v.end(), header.v.begin(), header.v.end());
    file.v.insert(file.v.end(), payload.v.begin(), payload.v.end());
}
struct ExsZone
{
    int key = 60, keyLow = 60, keyHigh = 60, velocityLow = 0, velocityHigh = 127;
    int sampleIndex = 0;
    juce::uint32 sampleStart = 0, sampleEnd = 1000, loopStart = 0, loopEnd = 0;
    bool oneshot = true;
};
struct ExsSample { juce::String fileName, directory; };
juce::File writeExs(TestDirectory& t, const juce::String& relative,
                    const std::vector<ExsZone>& zones, const std::vector<ExsSample>& samples)
{
    Bytes file;
    Bytes instrument;
    instrument.u32(0); instrument.u32((juce::uint32) zones.size());
    instrument.u32(0); instrument.u32((juce::uint32) samples.size());
    pad(instrument, 40);
    writeBlock(file, 0, 0, "(null)", instrument);
    for (int i = 0; i < (int) zones.size(); ++i)
    {
        const auto& z = zones[(size_t) i];
        Bytes payload;
        payload.u8(z.oneshot ? 0x49 : 0x48);
        payload.u8(z.key); payload.u8(0); payload.u8(0); payload.u8(0); payload.u8(0);
        payload.u8(z.keyLow); payload.u8(z.keyHigh); payload.u8(0);
        payload.u8(z.velocityLow); payload.u8(z.velocityHigh); payload.u8(0);
        payload.u8(0); payload.u8(0); payload.u8(0); payload.u8(0);
        payload.u32(z.sampleEnd); payload.u32(z.sampleStart);
        payload.u32(z.loopEnd); payload.u32(z.loopStart); payload.u32(0);
        payload.v[33] = z.oneshot ? 0 : 1; // loop options bit 0
        pad(payload, 88);
        payload.i32(-1);
        payload.u32((juce::uint32) z.sampleIndex);
        pad(payload, 108);
        writeBlock(file, 1, i, "zone " + juce::String(i), payload);
    }
    for (int i = 0; i < (int) samples.size(); ++i)
    {
        const auto& s = samples[(size_t) i];
        Bytes payload;
        payload.u32(44); payload.u32(1000); payload.u32(44100); payload.u32(24);
        payload.u32(2); payload.u32(2); payload.u32(0);
        payload.v.insert(payload.v.end(), { 'W', 'A', 'V', 'E' });
        payload.u32(0); payload.u32(0);
        pad(payload, 80);
        auto dir = s.directory.toRawUTF8();
        const int n = juce::jmin(255, (int) s.directory.getNumBytesAsUTF8());
        payload.v.insert(payload.v.end(), dir, dir + n);
        pad(payload, 336);
        writeBlock(file, 3, i, s.fileName, payload);
    }
    const auto path = t.dir.getChildFile(relative);
    path.getParentDirectory().createDirectory();
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
juce::String encodeK2Path(const juce::String& relative)
{
    juce::StringArray parts;
    parts.addTokens(relative.replaceCharacter('\\', '/'), "/", "");
    juce::String out = "@";
    for (const auto& part : parts)
    {
        if (part == "..") out << "b";
        else if (part.containsChar('.'))
        {
            const int code = (int) part.getNumBytesAsUTF8();
            out << "F-0001" << juce::String(code).paddedLeft('0', 3) << "000" << part;
        }
        else
        {
            out << "d" << juce::String((int) part.getNumBytesAsUTF8()).paddedLeft('0', 3) << part;
        }
    }
    return out;
}
juce::String k2Xml(const juce::String& name, const std::vector<ExsZone>& zones,
                   const std::vector<juce::String>& files, bool niss)
{
    juce::String x;
    x << "<?xml version=\"1.0\"?>\n";
    x << (niss ? "<NiSS_Program name=\"" : "<K2_Container name=\"")
      << name << "\" version=\"0.50\">\n";
    x << (niss ? "<Zones>\n" : "<Programs><K2_Program name=\"Prog\"><Zones>\n");
    for (int i = 0; i < (int) zones.size(); ++i)
    {
        const auto& z = zones[(size_t) i];
        x << (niss ? "<NiSS_Zone index=\"" : "<K2_Zone index=\"") << i << "\">\n<Parameters>\n";
        auto add = [&](const juce::String& n, const juce::String& v) { x << "<V name=\"" << n << "\" value=\"" << v << "\"/>\n"; };
        add("sampleStart", juce::String(z.sampleStart));
        add("sampleEnd", juce::String(z.sampleEnd));
        add("lowVelocity", juce::String(juce::jmax(1, z.velocityLow)));
        add("highVelocity", juce::String(z.velocityHigh));
        add("lowKey", juce::String(z.keyLow));
        add("highKey", juce::String(z.keyHigh));
        add("rootKey", juce::String(z.key));
        add("zoneTune", "1.000000");
        x << "</Parameters>\n<Sample>\n";
        add(niss ? "file" : "file_ex2", files[(size_t) i]);
        x << "</Sample>\n" << (niss ? "</NiSS_Zone>\n" : "</K2_Zone>\n");
    }
    x << "</Zones>\n" << (niss ? "</NiSS_Program>\n" : "</K2_Program></Programs></K2_Container>\n");
    return x;
}
juce::File writeNki(TestDirectory& t, const juce::String& relative, const juce::String& xml,
                    juce::uint32 magic = 0x7fa89012u)
{
    juce::MemoryOutputStream compressed;
    z_stream stream {};
    REQUIRE(deflateInit(&stream, Z_DEFAULT_COMPRESSION) == Z_OK);
    const auto utf = xml.toRawUTF8();
    stream.next_in = (Bytef*) utf;
    stream.avail_in = (uInt) xml.getNumBytesAsUTF8();
    std::vector<juce::uint8> output(compressBound(stream.avail_in));
    stream.next_out = output.data();
    stream.avail_out = (uInt) output.size();
    REQUIRE(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    const auto produced = output.size() - stream.avail_out;
    deflateEnd(&stream);
    Bytes file;
    file.u32(magic);
    pad(file, 16);
    file.v.insert(file.v.end(), output.begin(), output.begin() + (long) produced);
    const auto path = t.dir.getChildFile(relative);
    path.getParentDirectory().createDirectory();
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
}
TEST_CASE("EXS24 reader maps zones, samples and relative fallback", "[sampler][exs]")
{
    TestDirectory t;
    t.wave("Drums/samples/Kick.wav", 220, 2, 1000);
    t.wave("Drums/samples/Snare.wav", 660, 1, 2000);
    ExsZone kick;
    kick.key = 36; kick.keyLow = 36; kick.keyHigh = 36; kick.sampleIndex = 0;
    kick.sampleStart = 0; kick.sampleEnd = 1000;
    ExsZone snare;
    snare.key = 38; snare.keyLow = 38; snare.keyHigh = 38; snare.sampleIndex = 1;
    snare.sampleStart = 100; snare.sampleEnd = 1500; snare.oneshot = false;
    snare.loopStart = 200; snare.loopEnd = 800;
    auto file = writeExs(t, "Drums/Kit.exs", { kick, snare },
        { { "Kick.wav", "Macintosh HD:Old:Place:Drums:samples:" },
          { "Snare.wav", "Macintosh HD:Old:Place:Drums:samples:" } });
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::exs24, instrument, error));
    REQUIRE(error.isEmpty());
    REQUIRE(instrument.sources.size() == 2);
    REQUIRE(instrument.sources[0].name == "Kick.wav");
    REQUIRE(instrument.sources[0].frameCount == 1000);
    REQUIRE(instrument.sources[0].channels == 2);
    REQUIRE(instrument.sources[1].frameCount == 2000);
    REQUIRE(instrument.audio[0] != nullptr);
    REQUIRE(instrument.audio[1] != nullptr);
    REQUIRE(instrument.zones.size() == 2);
    REQUIRE(instrument.zones[0].sampleIndex == 0);
    REQUIRE(instrument.zones[0].keyLow == 36);
    REQUIRE(instrument.zones[0].rootKey == 36);
    REQUIRE_FALSE(instrument.zones[0].looping);
    REQUIRE(instrument.zones[1].sampleIndex == 1);
    REQUIRE(instrument.zones[1].start == 100);
    REQUIRE(instrument.zones[1].end == 1500);
    REQUIRE(instrument.zones[1].looping);
    REQUIRE(instrument.zones[1].loopStart == 200);
    REQUIRE(instrument.zones[1].loopEnd == 800);
}
TEST_CASE("EXS24 missing and ambiguous samples fail explicitly", "[sampler][exs]")
{
    TestDirectory t;
    t.wave("Drums/a/Kick.wav", 220, 1, 500);
    t.wave("Drums/b/Kick.wav", 330, 1, 500);
    ExsZone zone; zone.key = 36; zone.keyLow = 36; zone.keyHigh = 36;
    auto ambiguous = writeExs(t, "Drums/Ambiguous.exs", { zone },
        { { "Kick.wav", "Macintosh HD:No:Such:Folder:" } });
    FileInstrument instrument; juce::String error;
    REQUIRE_FALSE(loadFileInstrument(ambiguous, FileInstrumentFormat::exs24, instrument, error));
    REQUIRE(error.containsIgnoreCase("ambiguous"));
    auto missing = writeExs(t, "Drums/Missing.exs", { zone },
        { { "NotThere.wav", "Macintosh HD:No:Such:Folder:" } });
    error.clear();
    REQUIRE_FALSE(loadFileInstrument(missing, FileInstrumentFormat::exs24, instrument, error));
    REQUIRE(error.containsIgnoreCase("resolve"));
    juce::File bad = t.dir.getChildFile("Drums/Bad.exs");
    REQUIRE(bad.replaceWithText("not an exs file"));
    error.clear();
    REQUIRE_FALSE(loadFileInstrument(bad, FileInstrumentFormat::exs24, instrument, error));
}
TEST_CASE("NKI K2 and NiSS XML zones map to external samples", "[sampler][nki]")
{
    TestDirectory t;
    t.wave("Samples/Kick.wav", 220, 1, 1000);
    t.wave("Samples/Snare.wav", 660, 2, 2000);
    ExsZone kick; kick.key = 36; kick.keyLow = 36; kick.keyHigh = 36; kick.sampleEnd = 1000;
    ExsZone snare; snare.key = 38; snare.keyLow = 38; snare.keyHigh = 38;
    snare.velocityLow = 64; snare.sampleEnd = 2000;
    auto k2 = writeNki(t, "Kontakt/Kit.nki",
        k2Xml("Kit", { kick, snare }, { encodeK2Path("../Samples/Kick.wav"), encodeK2Path("../Samples/Snare.wav") }, false));
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(k2, FileInstrumentFormat::nki, instrument, error));
    REQUIRE(error.isEmpty());
    REQUIRE(instrument.name == "Kit");
    REQUIRE(instrument.sources.size() == 2);
    REQUIRE(instrument.sources[0].name == "Kick.wav");
    REQUIRE(instrument.sources[1].frameCount == 2000);
    REQUIRE(instrument.zones[0].keyLow == 36);
    REQUIRE(instrument.zones[1].velocityLow == 64);
    auto niss = writeNki(t, "Kontakt/Niss.nki",
        k2Xml("Niss Kit", { kick }, { "..\\Samples\\Kick.wav" }, true), 0x5ee56eb3u);
    error.clear();
    FileInstrument nissInstrument;
    REQUIRE(loadFileInstrument(niss, FileInstrumentFormat::nki, nissInstrument, error));
    REQUIRE(nissInstrument.sources.size() == 1);
    REQUIRE(nissInstrument.zones.size() == 1);
}
TEST_CASE("Unsupported NKI containers are reported, not guessed", "[sampler][nki]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("Kontakt/Unknown.nki");
    file.getParentDirectory().createDirectory();
    Bytes bytes; bytes.u32(0x4e205c2fu); pad(bytes, 32);
    REQUIRE(file.replaceWithData(bytes.v.data(), bytes.v.size()));
    FileInstrument instrument; juce::String error;
    REQUIRE_FALSE(loadFileInstrument(file, FileInstrumentFormat::nki, instrument, error));
    REQUIRE(error.containsIgnoreCase("unsupported"));
}
TEST_CASE("External instrument sampler renders zones and reports triggers", "[sampler][external]")
{
    TestDirectory t;
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1; audio.frames = 44100;
    audio.left.assign(44100, 0.5f);
    FileInstrument instrument;
    instrument.name = "Test";
    SampleDescriptor d; d.id = 0; d.name = "tone"; d.sampleRate = 44100; d.channels = 1; d.frameCount = 44100;
    instrument.sources.push_back(d);
    instrument.audio.push_back(std::make_shared<const SampleAudio>(audio));
    ExternalZone zone;
    zone.sampleIndex = 0; zone.keyLow = 36; zone.keyHigh = 36; zone.rootKey = 36;
    zone.start = 0; zone.end = 44100;
    instrument.zones.push_back(zone);
    ExternalKitSampler sampler; juce::String error;
    REQUIRE(sampler.load(instrument, error));
    sampler.setGeneration(7);
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    sampler.render(buffer, 0, 512, midi);
    REQUIRE(sampler.activeVoices() == 1);
    REQUIRE(buffer.getMagnitude(0, 512) > 0.05f);
    const auto trigger = sampler.lastTrigger();
    REQUIRE(trigger.valid);
    REQUIRE(trigger.generation == 7);
    REQUIRE(trigger.sourceId == 0);
}
TEST_CASE("Preview exposes EXS24 samples, waveform and source export", "[sampler][ui][exs]")
{
    TestDirectory t;
    t.wave("Drums/samples/Kick.wav", 220, 1, 4410);
    t.wave("Drums/samples/Snare.wav", 660, 1, 8820);
    ExsZone kick; kick.key = 36; kick.keyLow = 36; kick.keyHigh = 36; kick.sampleEnd = 4410;
    ExsZone snare; snare.key = 38; snare.keyLow = 38; snare.keyHigh = 38; snare.sampleEnd = 8820;
    auto file = writeExs(t, "Drums/Kit.exs", { kick, snare },
        { { "Kick.wav", "Macintosh HD:Old:Drums:samples:" },
          { "Snare.wav", "Macintosh HD:Old:Drums:samples:" } });
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    REQUIRE(engine.isSamplerInstrumentMode());
    REQUIRE_FALSE(engine.isSoundFontMode());
    REQUIRE(preview.instrumentSampleCount() == 2);
    REQUIRE(preview.selectedInstrumentSampleId() == 0);
    REQUIRE(preview.waveformLength() == Catch::Approx(0.1).margin(0.001));
    auto* box = dynamic_cast<juce::ComboBox*>(preview.findChildWithID("instrumentSample"));
    REQUIRE(box != nullptr);
    REQUIRE_FALSE(box->isVisible());
    auto& sources = preview.containerSourceList();
    REQUIRE(sources.isVisible());
    REQUIRE(sources.getListBoxModel()->getNumRows() == 3);
    sources.selectRow(2);
    sources.getListBoxModel()->selectedRowsChanged(2);
    REQUIRE(preview.selectedInstrumentSampleId() == 1);
    REQUIRE(preview.waveformLength() == Catch::Approx(0.2).margin(0.001));
    preview.selectRange(0.02, 0.06);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(preview.preparedSelection()));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 1764);
    REQUIRE(reader->numChannels == 1);
    REQUIRE(engine.setMidiPreview(true));
    juce::AudioBuffer<float> midiBuffer(2, 256);
    engine.preparePreviewBlock(256);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    engine.renderPreviewBlock(midiBuffer, 256);
    REQUIRE(engine.instrumentActiveVoices() == 1);
    REQUIRE(midiBuffer.getMagnitude(0, 256) > 0.001f);
    REQUIRE(engine.lastInstrumentMidiNote() == 36);
    REQUIRE(engine.lastInstrumentMidiLayers() == 1);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 65, 1.0f));
    engine.renderPreviewBlock(midiBuffer, 256);
    REQUIRE(engine.lastInstrumentMidiNote() == 65);
    REQUIRE(engine.lastInstrumentMidiLayers() == 0);
}
TEST_CASE("Preview exposes NKI XML samples and waveform", "[sampler][ui][nki]")
{
    TestDirectory t;
    t.wave("Samples/Kick.wav", 220, 1, 4410);
    ExsZone kick; kick.key = 36; kick.keyLow = 36; kick.keyHigh = 36; kick.sampleEnd = 4410;
    auto file = writeNki(t, "Kontakt/Kit.nki", k2Xml("Kit", { kick }, { encodeK2Path("../Samples/Kick.wav") }, false));
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    REQUIRE(engine.isSamplerInstrumentMode());
    REQUIRE(preview.instrumentSampleCount() == 1);
    REQUIRE(preview.selectedInstrumentSampleId() == 0);
    REQUIRE(preview.waveformLength() == Catch::Approx(0.1).margin(0.001));
    REQUIRE(engine.setMidiPreview(true));
    juce::AudioBuffer<float> buffer(2, 128);
    engine.preparePreviewBlock(128);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    engine.renderPreviewBlock(buffer, 128);
    REQUIRE(engine.instrumentActiveVoices() == 1);
    REQUIRE(engine.lastInstrumentTrigger().sourceId == 0);
}
TEST_CASE("EXS24 and NKI assets are indexed metadata-only", "[sampler][indexing]")
{
    TestDirectory t;
    t.wave("Drums/samples/Kick.wav", 220, 1, 500);
    ExsZone zone; zone.key = 36; zone.keyLow = 36; zone.keyHigh = 36;
    auto exs = writeExs(t, "Drums/Kit.exs", { zone },
        { { "Kick.wav", "Macintosh HD:Old:Drums:samples:" } });
    REQUIRE(FileScanner::isSupportedFormat(exs));
    auto record = analyzeMetadataAsset(exs);
    REQUIRE(record.version == metadataOnlyVersion);
    REQUIRE(record.error.isEmpty());
    auto nki = writeNki(t, "Kontakt/Kit.nki",
        k2Xml("Kit", { zone }, { encodeK2Path("../Drums/samples/Kick.wav") }, false));
    REQUIRE(FileScanner::isSupportedFormat(nki));
    record = analyzeMetadataAsset(nki);
    REQUIRE(record.version == metadataOnlyVersion);
    REQUIRE(record.error.isEmpty());
    auto unsupported = t.dir.getChildFile("Kontakt/Old.nki");
    Bytes bytes; bytes.u32(0x4e205c2fu); pad(bytes, 32);
    REQUIRE(unsupported.replaceWithData(bytes.v.data(), bytes.v.size()));
    record = analyzeMetadataAsset(unsupported);
    REQUIRE(record.error.containsIgnoreCase("unsupported"));
    auto broken = t.dir.getChildFile("Drums/Broken.exs");
    REQUIRE(broken.replaceWithText("not an instrument"));
    record = analyzeMetadataAsset(broken);
    REQUIRE(record.error.isNotEmpty());
}
TEST_CASE("Decoded 24-bit sampler sources export as 24-bit PCM", "[sampler][export]")
{
    TestDirectory t;
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1; audio.frames = 4410;
    audio.bitsPerSample = 24; audio.floatingPoint = false;
    audio.left.assign(4410, 0.25f);
    const auto result = AudioExporter().exportSampleSelection(audio, "source", 0.0, 0.05,
                                                              t.dir.getChildFile("exports"));
    REQUIRE(result.success);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(result.file));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->bitsPerSample == 24);
    REQUIRE_FALSE(reader->usesFloatingPointData);
    REQUIRE(reader->lengthInSamples == 2205);
}
TEST_CASE("External sampler advances fractional pitch increments", "[sampler][external]")
{
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1; audio.frames = 44100;
    audio.left.assign(44100, 0.0f);
    audio.left[1000] = 0.8f;
    FileInstrument instrument;
    SampleDescriptor d; d.id = 0; d.name = "impulse"; d.sampleRate = 44100; d.channels = 1; d.frameCount = 44100;
    instrument.sources.push_back(d);
    instrument.audio.push_back(std::make_shared<const SampleAudio>(audio));
    ExternalZone zone;
    zone.sampleIndex = 0; zone.keyLow = 36; zone.keyHigh = 36; zone.rootKey = 60;
    zone.start = 0; zone.end = 44100;
    instrument.zones.push_back(zone);
    ExternalKitSampler sampler; juce::String error;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(1, 8192);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    sampler.render(buffer, 0, 8192, midi);
    REQUIRE(buffer.getMagnitude(0, 8192) > 0.1f);
}
TEST_CASE("Auto Preview starts an EXS sample on load", "[sampler][ui][exs]")
{
    TestDirectory t;
    t.wave("Drums/samples/Kick.wav", 220, 1, 4410);
    ExsZone zone; zone.key = 36; zone.keyLow = 36; zone.keyHigh = 36; zone.sampleEnd = 4410;
    auto file = writeExs(t, "Drums/Kit.exs", { zone },
        { { "Kick.wav", "Macintosh HD:Old:Drums:samples:" } });
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file));
    REQUIRE(engine.isSamplerInstrumentMode());
    REQUIRE(engine.isPlaying());
}
TEST_CASE("One-shot external zones ignore NoteOff and ring out", "[sampler][external]")
{
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1; audio.frames = 44100;
    audio.left.assign(44100, 0.0f);
    audio.left[10000] = 0.8f;
    FileInstrument instrument;
    SampleDescriptor d; d.id = 0; d.name = "impulse"; d.sampleRate = 44100; d.channels = 1; d.frameCount = 44100;
    instrument.sources.push_back(d);
    instrument.audio.push_back(std::make_shared<const SampleAudio>(audio));
    ExternalZone zone;
    zone.sampleIndex = 0; zone.keyLow = 36; zone.keyHigh = 36; zone.rootKey = 36;
    zone.start = 0; zone.end = 44100; zone.oneShot = true;
    instrument.zones.push_back(zone);
    ExternalKitSampler sampler; juce::String error;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(1, 16384);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 2048);
    sampler.render(buffer, 0, 16384, midi);
    REQUIRE(buffer.getMagnitude(0, 16384) > 0.1f);
}

TEST_CASE("External MIDI follow preserves overlapping voices through the device player", "[.device][sampler][midi-tails]")
{
    TestDirectory t;
    t.wave("Samples/A.wav", 220, 1, 44100);
    t.wave("Samples/B.wav", 660, 1, 44100);
    ExsZone a; a.key = a.keyLow = a.keyHigh = 36; a.sampleEnd = 44100;
    ExsZone b = a; b.key = b.keyLow = b.keyHigh = 38; b.sampleIndex = 1;
    const auto file = writeExs(t, "Kit.exs", { a, b }, { { "A.wav", "Samples:" }, { "B.wav", "Samples:" } });
    AudioEngine engine(true);
    REQUIRE(engine.deviceManager().getCurrentAudioDevice() != nullptr);
    const juce::ScopedLock callbackGuard(engine.deviceManager().getAudioCallbackLock());
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    engine.preparePreviewBlock(256);
    juce::AudioBuffer<float> buffer(2, 256);
    for (int note : { 36, 38, 36 })
    {
        engine.midiCollector().push(juce::MidiMessage::noteOn(1, note, 1.0f));
        engine.renderPreviewBlock(buffer, 256);
        const auto voices = engine.instrumentActiveVoices();
        preview.refreshMidiFollow();
        REQUIRE(engine.instrumentActiveVoices() == voices);
        engine.midiCollector().push(juce::MidiMessage::noteOff(1, note));
        engine.renderPreviewBlock(buffer, 256);
    }
    REQUIRE(engine.instrumentActiveVoices() == 3);
    REQUIRE_FALSE(engine.isPlaying());
    REQUIRE(buffer.getMagnitude(0, 256) > 0.001f);
}

TEST_CASE("External sustain voices release over two seconds", "[sampler][external][midi-tails]")
{
    SampleAudio audio;
    audio.sampleRate = 1000; audio.channels = 1; audio.frames = 4000;
    audio.left.assign(4000, 0.5f);
    FileInstrument instrument;
    SampleDescriptor d; d.id = 0; d.frameCount = 4000; d.sampleRate = 1000; d.channels = 1;
    instrument.sources.push_back(d);
    instrument.audio.push_back(std::make_shared<const SampleAudio>(audio));
    ExternalZone zone; zone.sampleIndex = 0; zone.end = 4000;
    instrument.zones.push_back(zone);
    ExternalKitSampler sampler; juce::String error;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(1000);
    juce::AudioBuffer<float> buffer(1, 2100);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 10, midi);
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    sampler.render(buffer, 0, 1000, midi);
    REQUIRE(sampler.activeVoices() == 1);
    REQUIRE(buffer.getSample(0, 999) == Catch::Approx(0.0625f).margin(0.001f));
    midi.clear(); sampler.render(buffer, 0, 1100, midi);
    REQUIRE(sampler.activeVoices() == 0);
    REQUIRE(buffer.getSample(0, 1099) == 0.0f);
}

TEST_CASE("External sampler keeps 128 voices and panic still silences them", "[sampler][external][midi-tails]")
{
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1; audio.frames = 4410;
    audio.left.assign(4410, 0.5f);
    FileInstrument instrument;
    SampleDescriptor d; d.id = 0; d.frameCount = 4410; d.sampleRate = 44100; d.channels = 1;
    instrument.sources.push_back(d);
    instrument.audio.push_back(std::make_shared<const SampleAudio>(audio));
    ExternalZone zone; zone.sampleIndex = 0; zone.end = 4410; zone.oneShot = true;
    instrument.zones.push_back(zone);
    ExternalKitSampler sampler; juce::String error;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(1, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 128; ++i) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 512, midi);
    REQUIRE(sampler.activeVoices() == 128);
    const auto sequence = sampler.lastTrigger().sequence;
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 512, midi);
    REQUIRE(sampler.activeVoices() == 128);
    REQUIRE(sampler.lastTrigger().sequence == sequence);
    midi.clear(); midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    sampler.render(buffer, 0, 512, midi);
    REQUIRE(sampler.activeVoices() == 0);
    REQUIRE(buffer.getMagnitude(0, 512) == 0.0f);
}
