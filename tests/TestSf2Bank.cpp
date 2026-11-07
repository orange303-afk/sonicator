#include <catch2/catch_test_macros.hpp>
#include "core/sf2/Sf2Bank.h"
#include "TestHelpers.h"
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
void chunk(Bytes& out, const char* id, Bytes payload)
{
    out.ascii(id); out.u32((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
Bytes presetHeader(const juce::String& name, int program, int bank, int bagIndex)
{
    Bytes b; b.name20(name); b.u16((juce::uint16) program); b.u16((juce::uint16) bank); b.u16((juce::uint16) bagIndex);
    b.u32(0); b.u32(0); b.u32(0);
    return b;
}
Bytes sampleHeader(const juce::String& name, int start, int end, int root)
{
    Bytes b; b.name20(name); b.u32((juce::uint32) start); b.u32((juce::uint32) end);
    b.u32((juce::uint32) start); b.u32((juce::uint32) end); b.u32(44100);
    b.v.push_back((juce::uint8) root); b.v.push_back(0); b.u16(0); b.u16(1);
    return b;
}
juce::File writeBank(TestDirectory& t)
{
    Bytes pcm; for (int i = 0; i < 8; ++i) pcm.i16((juce::int16) (1000 + i * 100));
    Bytes sdta; sdta.ascii("sdta"); chunk(sdta, "smpl", pcm);
    Bytes info; info.ascii("INFO"); Bytes inam; inam.ascii("Test Bank"); inam.v.push_back(0); chunk(info, "INAM", inam);

    // preset 0 (Drums) uses bag 0; preset 1 (Piano) uses the empty bag 1; bag 2 is terminal.
    Bytes pbag; pbag.u16(0); pbag.u16(0); pbag.u16(1); pbag.u16(0); pbag.u16(1); pbag.u16(0);
    Bytes pgen; pgen.u16(41); pgen.u16(0);

    // instrument 0 has three zones, each with three generators.
    Bytes ibag;
    for (int start : { 0, 3, 6, 9, 9 }) { ibag.u16((juce::uint16) start); ibag.u16(0); }
    Bytes igen;
    auto keyRange = [](int low, int high) { return (juce::uint16) (low | (high << 8)); };
    auto addGen = [&](int oper, juce::uint16 amount) { igen.u16((juce::uint16) oper); igen.u16(amount); };
    addGen(43, keyRange(36, 36)); addGen(44, keyRange(0, 63)); addGen(53, 0);
    addGen(43, keyRange(36, 36)); addGen(44, keyRange(64, 127)); addGen(53, 1);
    addGen(43, keyRange(38, 38)); addGen(54, 1); addGen(53, 1);

    Bytes inst; inst.name20("Kit"); inst.u16(0);
    Bytes instTerminal; instTerminal.name20("EOT"); instTerminal.u16(4);
    inst.v.insert(inst.v.end(), instTerminal.v.begin(), instTerminal.v.end());

    Bytes shdr;
    auto appendHeader = [&](Bytes header) { shdr.v.insert(shdr.v.end(), header.v.begin(), header.v.end()); };
    appendHeader(sampleHeader("snare", 0, 4, 60));
    appendHeader(sampleHeader("snare2", 0, 4, 60));
    appendHeader(sampleHeader("hat", 4, 8, 62));
    appendHeader(sampleHeader("hat2", 4, 8, 62));
    Bytes shdrTerminal; shdrTerminal.name20("EOS"); for (int i = 0; i < 26; ++i) shdrTerminal.v.push_back(0);
    appendHeader(shdrTerminal);

    Bytes pdta; pdta.ascii("pdta");
    Bytes phdr;
    auto appendPreset = [&](Bytes header) { phdr.v.insert(phdr.v.end(), header.v.begin(), header.v.end()); };
    appendPreset(presetHeader("Drums", 0, 128, 0));
    appendPreset(presetHeader("Piano", 0, 0, 1));
    appendPreset(presetHeader("EOP", 0, 0, 2));
    chunk(pdta, "phdr", phdr); chunk(pdta, "pbag", pbag); chunk(pdta, "pgen", pgen);
    chunk(pdta, "inst", inst); chunk(pdta, "ibag", ibag); chunk(pdta, "igen", igen); chunk(pdta, "shdr", shdr);

    Bytes infoList; infoList.ascii("LIST"); infoList.u32((juce::uint32) info.v.size()); infoList.v.insert(infoList.v.end(), info.v.begin(), info.v.end());
    Bytes sdtaList; sdtaList.ascii("LIST"); sdtaList.u32((juce::uint32) sdta.v.size()); sdtaList.v.insert(sdtaList.v.end(), sdta.v.begin(), sdta.v.end());
    Bytes pdtaList; pdtaList.ascii("LIST"); pdtaList.u32((juce::uint32) pdta.v.size()); pdtaList.v.insert(pdtaList.v.end(), pdta.v.begin(), pdta.v.end());
    Bytes body; body.ascii("sfbk");
    body.v.insert(body.v.end(), infoList.v.begin(), infoList.v.end());
    body.v.insert(body.v.end(), sdtaList.v.begin(), sdtaList.v.end());
    body.v.insert(body.v.end(), pdtaList.v.begin(), pdtaList.v.end());
    Bytes file; file.ascii("RIFF"); file.u32((juce::uint32) body.v.size()); file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile("test.sf2");
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
}
TEST_CASE("SF2 bank parses presets, zones, velocity layers and PCM", "[sf2][bank]")
{
    TestDirectory t;
    auto path = writeBank(t);
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    INFO(error);
    REQUIRE(bank.isLoaded());
    REQUIRE(bank.name() == "Test Bank");
    REQUIRE(bank.presets().size() == 1); // the empty Piano preset is dropped
    const auto& preset = bank.presets().front();
    REQUIRE(preset.name == "Drums");
    REQUIRE(preset.bank == 128);
    REQUIRE(bank.isDrumPreset(preset));
    REQUIRE(bank.firstDrumPreset() == 0);
    REQUIRE(preset.zones.size() == 3);
    REQUIRE(preset.zones[0].keyLow == 36);
    REQUIRE(preset.zones[0].velocityHigh == 63);
    REQUIRE(preset.zones[1].velocityLow == 64);
    REQUIRE(preset.zones[1].sampleIndex == 1);
    REQUIRE(preset.zones[2].keyLow == 38);
    REQUIRE(preset.zones[2].loopMode == 1);
    REQUIRE(bank.samples().size() == 4);
    REQUIRE(bank.pcm().size() == 8);
    REQUIRE(bank.pcm()[0] == 1000);
    REQUIRE(bank.pcm()[7] == 1700);
}
TEST_CASE("A non-SoundFont file is rejected", "[sf2][bank]")
{
    TestDirectory t;
    auto path = t.dir.getChildFile("not.sf2");
    const char bytes[] = "RIFFxxxxWAVE";
    path.replaceWithData(bytes, sizeof(bytes));
    Sf2Bank bank; juce::String error;
    REQUIRE_FALSE(bank.load(path, error));
    REQUIRE(error.isNotEmpty());
}
