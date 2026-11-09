#include <catch2/catch_test_macros.hpp>
#include "core/extract/EmbeddedSamples.h"
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include <cmath>
using namespace sonicator;
namespace {
struct Bytes
{
    std::vector<juce::uint8> v;
    void ascii(const juce::String& s) { v.insert(v.end(), s.toRawUTF8(), s.toRawUTF8() + s.getNumBytesAsUTF8()); }
    void u16(juce::uint16 x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
    void u32(juce::uint32 x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); }
    void i16(juce::int16 x) { u16((juce::uint16) x); }
    void name20(const juce::String& name)
    {
        auto bytes = name.toRawUTF8();
        const int count = juce::jmin(19, (int) name.getNumBytesAsUTF8());
        v.insert(v.end(), bytes, bytes + count);
        while (v.size() % 20 != 0) v.push_back(0);
    }
};
void chunk(Bytes& out, const char* id, const Bytes& payload)
{
    out.ascii(id); out.u32((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
Bytes presetHeader(const juce::String& name, int bank, int bagIndex)
{
    Bytes b; b.name20(name); b.u16(0); b.u16((juce::uint16) bank); b.u16((juce::uint16) bagIndex);
    b.u32(0); b.u32(0); b.u32(0);
    return b;
}
Bytes sampleHeader(const juce::String& name, int start, int end, int type, int link, int root = 60)
{
    Bytes b; b.name20(name); b.u32((juce::uint32) start); b.u32((juce::uint32) end);
    b.u32((juce::uint32) start); b.u32((juce::uint32) end); b.u32(44100);
    b.v.push_back((juce::uint8) root); b.v.push_back(0); b.u16((juce::uint16) link); b.u16((juce::uint16) type);
    return b;
}
// Builds a bank whose shdr lists the supplied headers in order and whose single preset zone
// references sample 0. PCM is one frame per index range, all 100 Hz sine at 0.5.
juce::File writeBank(TestDirectory& t, const juce::String& name,
                     const std::vector<Bytes>& headers, int pcmFrames)
{
    Bytes pcm;
    for (int i = 0; i < pcmFrames; ++i)
        pcm.i16((juce::int16) std::lround(0.5 * 32767.0
            * std::sin(2.0 * juce::MathConstants<double>::pi * 100.0 * i / 44100.0)));
    Bytes smpl; chunk(smpl, "smpl", pcm);
    Bytes sdta; sdta.ascii("sdta"); sdta.v.insert(sdta.v.end(), smpl.v.begin(), smpl.v.end());
    Bytes info; info.ascii("INFO");

    Bytes pbag; pbag.u16(0); pbag.u16(0); pbag.u16(1); pbag.u16(0);
    Bytes pgen; pgen.u16(41); pgen.u16(0);
    Bytes ibag;
    ibag.u16(0); ibag.u16(0);
    ibag.u16(2); ibag.u16(0);
    ibag.u16(2); ibag.u16(0);
    Bytes igen;
    igen.u16(43); igen.u16((juce::uint16) (0 | (127 << 8)));
    igen.u16(53); igen.u16(0);
    Bytes inst; inst.name20("Kit"); inst.u16(0);
    Bytes instTerminal; instTerminal.name20("EOT"); instTerminal.u16(2);
    inst.v.insert(inst.v.end(), instTerminal.v.begin(), instTerminal.v.end());
    Bytes shdr;
    for (const auto& header : headers)
        shdr.v.insert(shdr.v.end(), header.v.begin(), header.v.end());
    Bytes end; end.name20("EOS"); for (int i = 0; i < 26; ++i) end.v.push_back(0);
    shdr.v.insert(shdr.v.end(), end.v.begin(), end.v.end());

    Bytes pdta; pdta.ascii("pdta");
    Bytes phdr;
    auto addPreset = [&](const Bytes& b) { phdr.v.insert(phdr.v.end(), b.v.begin(), b.v.end()); };
    addPreset(presetHeader("Kit", 128, 0));
    addPreset(presetHeader("EOP", 0, 1));
    chunk(pdta, "phdr", phdr); chunk(pdta, "pbag", pbag); chunk(pdta, "pgen", pgen);
    chunk(pdta, "inst", inst); chunk(pdta, "ibag", ibag); chunk(pdta, "igen", igen); chunk(pdta, "shdr", shdr);

    Bytes infoList; infoList.ascii("LIST"); infoList.u32((juce::uint32) info.v.size());
    infoList.v.insert(infoList.v.end(), info.v.begin(), info.v.end());
    Bytes sdtaList; sdtaList.ascii("LIST"); sdtaList.u32((juce::uint32) sdta.v.size());
    sdtaList.v.insert(sdtaList.v.end(), sdta.v.begin(), sdta.v.end());
    Bytes pdtaList; pdtaList.ascii("LIST"); pdtaList.u32((juce::uint32) pdta.v.size());
    pdtaList.v.insert(pdtaList.v.end(), pdta.v.begin(), pdta.v.end());
    Bytes body; body.ascii("sfbk");
    body.v.insert(body.v.end(), infoList.v.begin(), infoList.v.end());
    body.v.insert(body.v.end(), sdtaList.v.begin(), sdtaList.v.end());
    body.v.insert(body.v.end(), pdtaList.v.begin(), pdtaList.v.end());
    Bytes file; file.ascii("RIFF"); file.u32((juce::uint32) body.v.size());
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
struct Blob
{
    std::vector<juce::uint8> v;
    void u8(int x) { v.push_back((juce::uint8) x); }
    void u16be(int x) { u8((x >> 8) & 0xff); u8(x & 0xff); }
    void ascii(const juce::String& text, size_t width)
    {
        const auto start = v.size();
        auto utf = text.toRawUTF8();
        const int n = juce::jmin((int) width, (int) text.getNumBytesAsUTF8());
        v.insert(v.end(), utf, utf + n);
        while (v.size() < start + width) v.push_back(0);
    }
};
juce::File writeMod(TestDirectory& t)
{
    Blob b;
    b.ascii("Test MOD", 20);
    for (int i = 0; i < 31; ++i)
    {
        if (i == 0)
        {
            b.ascii("sine", 22);
            b.u16be(32); b.u8(0); b.u8(64); b.u16be(0); b.u16be(32);
        }
        else
        {
            b.ascii(i == 1 ? "second" : "", 22);
            b.u16be(0); b.u8(0); b.u8(0); b.u16be(0); b.u16be(1);
        }
    }
    b.u8(1); b.u8(0);
    for (int i = 0; i < 128; ++i) b.u8(0);
    b.ascii("M.K.", 4);
    for (int i = 0; i < 4 * 64 * 4; ++i) b.u8(0);
    for (int i = 0; i < 64; ++i) b.u8((int8_t) (i * 2 - 64));
    auto file = t.dir.getChildFile("song.mod");
    file.replaceWithData(b.v.data(), b.v.size());
    return file;
}

juce::File writeModWithNotes(TestDirectory& t)
{
    Blob b;
    b.ascii("Note MOD", 20);
    const int lengths[31] = { 32, 32 };
    const char* names[31] = { "low", "high" };
    for (int i = 0; i < 31; ++i)
    {
        b.ascii(names[i], 22);
        b.u16be(lengths[i]); b.u8(0); b.u8(64); b.u16be(0); b.u16be(1);
    }
    b.u8(1); b.u8(0);
    for (int i = 0; i < 128; ++i) b.u8(0);
    b.ascii("M.K.", 4);
    std::vector<juce::uint8> pattern(4 * 64 * 4, 0);
    // Row 0, channel 0: sample 1 at C-4 (period 428).
    pattern[0] = 0x01; pattern[1] = 0xAC; pattern[2] = 0x10; pattern[3] = 0x00;
    // Row 0, channel 1: sample 2 at C-6 (period 107).
    pattern[4] = 0x00; pattern[5] = 0x6B; pattern[6] = 0x20; pattern[7] = 0x00;
    b.v.insert(b.v.end(), pattern.begin(), pattern.end());
    for (int i = 0; i < 64; ++i) b.u8((int8_t) (i * 2 - 64));
    for (int i = 0; i < 64; ++i) b.u8((int8_t) (i * 2 - 64));
    auto file = t.dir.getChildFile("notes.mod");
    file.replaceWithData(b.v.data(), b.v.size());
    return file;
}
} // namespace

TEST_CASE("Embedded listing reports every SF2 sample, including unused ones", "[extract][sf2]")
{
    TestDirectory t;
    auto path = writeBank(t, "two.sf2",
        { sampleHeader("used", 0, 100, 1, 0), sampleHeader("unused", 100, 200, 1, 0) }, 200);
    EmbeddedSampleSet set;
    juce::String error;
    REQUIRE(set.open(path, error));
    REQUIRE(error.isEmpty());
    const auto& entries = set.entries();
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].name == "used");
    REQUIRE(entries[1].name == "unused");
    const auto first = set.read(entries[0].index, error);
    REQUIRE(first != nullptr);
    REQUIRE(first->channels == 1);
    REQUIRE(first->frames == 100);
}

TEST_CASE("Embedded listing collapses an SF2 stereo pair into one entry", "[extract][sf2]")
{
    TestDirectory t;
    auto path = writeBank(t, "stereo.sf2",
        { sampleHeader("left", 0, 100, 4, 1), sampleHeader("right", 100, 200, 2, 0) }, 200);
    EmbeddedSampleSet set;
    juce::String error;
    REQUIRE(set.open(path, error));
    REQUIRE(set.entries().size() == 1);
    REQUIRE(set.entries()[0].name == "left");
    const auto audio = set.read(set.entries()[0].index, error);
    REQUIRE(audio != nullptr);
    REQUIRE(audio->channels == 2);
    REQUIRE(audio->right.size() == 100);
}

TEST_CASE("Embedded listing reads packed tracker PCM and skips empty slots", "[extract][tracker]")
{
    TestDirectory t;
    auto path = writeMod(t);
    EmbeddedSampleSet set;
    juce::String error;
    REQUIRE(set.open(path, error));
    REQUIRE(set.entries().size() == 1);
    REQUIRE(set.entries()[0].name == "sine");
    const auto audio = set.read(set.entries()[0].index, error);
    REQUIRE(audio != nullptr);
    REQUIRE(audio->frames == 64);
    REQUIRE(audio->channels == 1);
    REQUIRE(audio->sampleRate > 0);
}

TEST_CASE("Embedded listing rejects formats without packed audio", "[extract]")
{
    TestDirectory t;
    REQUIRE_FALSE(isEmbeddedSampleSource(t.wave()));
    REQUIRE_FALSE(isEmbeddedSampleSource(t.dir.getChildFile("bank.exs")));
    REQUIRE_FALSE(isEmbeddedSampleSource(t.dir.getChildFile("bank.nki")));
    REQUIRE(isEmbeddedSampleSource(t.dir.getChildFile("song.mod")));
    REQUIRE(isEmbeddedSampleSource(t.dir.getChildFile("song.xm")));
    REQUIRE(isEmbeddedSampleSource(t.dir.getChildFile("song.s3m")));
    REQUIRE(isEmbeddedSampleSource(t.dir.getChildFile("bank.sf2")));
    EmbeddedSampleSet set;
    juce::String error;
    REQUIRE_FALSE(set.open(t.dir.getChildFile("bank.exs"), error));
    REQUIRE(error.isNotEmpty());
}

TEST_CASE("Extract candidates include embedded sources and exclude ordinary audio", "[extract][menu]")
{
    TestDirectory t;
    auto module = writeMod(t);
    auto bank = writeBank(t, "one.sf2", { sampleHeader("used", 0, 64, 1, 0) }, 64);
    auto wave = t.wave("plain.wav");
    auto missing = t.dir.getChildFile("missing.mod");
    auto exs = t.dir.getChildFile("bank.exs");
    exs.replaceWithText("not audio");
    const auto candidates = embeddedSources({ wave, module, bank, missing, exs });
    REQUIRE(candidates.size() == 2);
    REQUIRE(candidates[0] == module);
    REQUIRE(candidates[1] == bank);
}


TEST_CASE("Module samples report the first note they are played at", "[extract][song]")
{
    TestDirectory t;
    auto path = writeModWithNotes(t);
    EmbeddedSampleSet set;
    juce::String error;
    REQUIRE(set.open(path, error));
    REQUIRE(set.entries().size() == 2);
    REQUIRE(set.playbackRatio(set.entries()[0].index) == Catch::Approx(1.0).margin(0.01));
    REQUIRE(set.playbackRatio(set.entries()[1].index) == Catch::Approx(4.0).margin(0.08));
    REQUIRE(set.playbackRatio(999) == 1.0);
}

TEST_CASE("Resampling scales frame count and keeps the waveform shape", "[extract][song]")
{
    SampleAudio audio;
    audio.sampleRate = 8000; audio.channels = 1; audio.frames = 100;
    audio.left.resize(100);
    for (int i = 0; i < 100; ++i) audio.left[(size_t) i] = (float) i / 100.0f;
    const auto faster = resampleForPlayback(audio, 2.0);
    REQUIRE(faster != nullptr);
    REQUIRE(faster->frames == 50);
    REQUIRE(faster->left[0] == Catch::Approx(0.0f).margin(0.001f));
    REQUIRE(faster->left[10] == Catch::Approx(0.2f).margin(0.01f));
    const auto slower = resampleForPlayback(audio, 0.5);
    REQUIRE(slower != nullptr);
    REQUIRE(slower->frames == 200);
    REQUIRE(slower->left[100] == Catch::Approx(0.5f).margin(0.01f));
    const auto unchanged = resampleForPlayback(audio, 1.0);
    REQUIRE(unchanged != nullptr);
    REQUIRE(unchanged->frames == 100);
}

