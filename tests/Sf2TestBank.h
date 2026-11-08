#pragma once
#include "TestHelpers.h"
#include "core/sf2/Sf2Bank.h"
#include <juce_core/juce_core.h>
#include <vector>
// Synthetic SoundFont 2 builder shared by the sampler and preview tests. Every table is built in
// fresh record buffers so name padding never misaligns the following 46/22/38-byte records.
namespace sf2test {
struct Bytes
{
    std::vector<juce::uint8> v;
    void ascii(const juce::String& s) { v.insert(v.end(), s.toRawUTF8(), s.toRawUTF8() + s.getNumBytesAsUTF8()); }
    void u16(juce::uint16 x) { v.push_back((juce::uint8) (x & 0xff)); v.push_back((juce::uint8) (x >> 8)); }
    void u32(juce::uint32 x) { for (int i = 0; i < 4; ++i) v.push_back((juce::uint8) ((x >> (8 * i)) & 0xff)); }
    void i16(juce::int16 x) { u16((juce::uint16) x); }
    void name20(const juce::String& name)
    {
        auto bytes = name.toRawUTF8();
        const int count = juce::jmin(19, (int) name.getNumBytesAsUTF8());
        v.insert(v.end(), bytes, bytes + count);
        while (v.size() % 20 != 0) v.push_back(0);
    }
};
inline void chunk(Bytes& out, const char* id, Bytes payload)
{
    out.ascii(id); out.u32((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
struct SpecSample
{
    juce::String name;
    int start = 0, end = 0, loopStart = 0, loopEnd = 0;
    int rate = 44100, root = 60, pitchCorrection = 0, link = 0, type = 1;
};
struct SpecZone
{
    int keyLow = 0, keyHigh = 127, velocityLow = 0, velocityHigh = 127;
    int sample = 0, loopMode = 0, rootKey = -1, exclusiveClass = 0;
};
struct SpecInstrument { juce::String name; std::vector<SpecZone> zones; };
struct SpecPart { int instrument = 0, keyLow = 0, keyHigh = 127, velocityLow = 0, velocityHigh = 127; };
struct SpecPreset { juce::String name; int bank = 0, program = 0; std::vector<SpecPart> parts; };
inline juce::File writeBank(TestDirectory& t, const std::vector<juce::int16>& pcm,
                     const std::vector<SpecSample>& samples,
                     const std::vector<SpecInstrument>& instruments,
                     const std::vector<SpecPreset>& presets)
{
    Bytes pcmBytes;
    for (auto value : pcm) pcmBytes.i16(value);
    Bytes sdta; sdta.ascii("sdta"); chunk(sdta, "smpl", pcmBytes);
    Bytes info; info.ascii("INFO"); Bytes inam; inam.ascii("Sample Bank"); inam.v.push_back(0);
    chunk(info, "INAM", inam);

    auto addRange = [](Bytes& out, int oper, int low, int high)
    { out.u16((juce::uint16) oper); out.u16((juce::uint16) (low | (high << 8))); };
    Bytes pbag, pgen;
    int pgenCount = 0;
    for (const auto& preset : presets)
        for (const auto& part : preset.parts)
        {
            pbag.u16((juce::uint16) pgenCount); pbag.u16(0);
            pgen.u16(41); pgen.u16((juce::uint16) part.instrument); ++pgenCount;
            if (part.keyLow != 0 || part.keyHigh != 127)
            { addRange(pgen, 43, part.keyLow, part.keyHigh); ++pgenCount; }
            if (part.velocityLow != 0 || part.velocityHigh != 127)
            { addRange(pgen, 44, part.velocityLow, part.velocityHigh); ++pgenCount; }
        }
    pbag.u16((juce::uint16) pgenCount); pbag.u16(0);

    Bytes ibag, igen;
    int igenCount = 0;
    for (const auto& instrument : instruments)
        for (const auto& zone : instrument.zones)
        {
            ibag.u16((juce::uint16) igenCount); ibag.u16(0);
            addRange(igen, 43, zone.keyLow, zone.keyHigh); ++igenCount;
            addRange(igen, 44, zone.velocityLow, zone.velocityHigh); ++igenCount;
            igen.u16(53); igen.u16((juce::uint16) zone.sample); ++igenCount;
            if (zone.loopMode != 0) { igen.u16(54); igen.u16((juce::uint16) zone.loopMode); ++igenCount; }
            if (zone.rootKey >= 0) { igen.u16(58); igen.u16((juce::uint16) zone.rootKey); ++igenCount; }
            if (zone.exclusiveClass != 0) { igen.u16(57); igen.u16((juce::uint16) zone.exclusiveClass); ++igenCount; }
        }
    ibag.u16((juce::uint16) igenCount); ibag.u16(0);

    Bytes inst;
    int bagIndex = 0;
    for (const auto& instrument : instruments)
    {
        Bytes record; record.name20(instrument.name); record.u16((juce::uint16) bagIndex);
        inst.v.insert(inst.v.end(), record.v.begin(), record.v.end());
        bagIndex += (int) instrument.zones.size();
    }
    Bytes instTerminal; instTerminal.name20("EOT"); instTerminal.u16((juce::uint16) bagIndex);
    inst.v.insert(inst.v.end(), instTerminal.v.begin(), instTerminal.v.end());

    Bytes shdr;
    for (const auto& sample : samples)
    {
        Bytes record;
        record.name20(sample.name);
        record.u32((juce::uint32) sample.start); record.u32((juce::uint32) sample.end);
        record.u32((juce::uint32) sample.loopStart); record.u32((juce::uint32) sample.loopEnd);
        record.u32((juce::uint32) sample.rate);
        record.v.push_back((juce::uint8) sample.root);
        record.v.push_back((juce::uint8) (juce::int8) sample.pitchCorrection);
        record.u16((juce::uint16) sample.link); record.u16((juce::uint16) sample.type);
        shdr.v.insert(shdr.v.end(), record.v.begin(), record.v.end());
    }
    Bytes shdrTerminal; shdrTerminal.name20("EOS");
    for (int i = 0; i < 26; ++i) shdrTerminal.v.push_back(0);
    shdr.v.insert(shdr.v.end(), shdrTerminal.v.begin(), shdrTerminal.v.end());

    Bytes pdta; pdta.ascii("pdta");
    Bytes phdr;
    int presetBagIndex = 0;
    for (const auto& preset : presets)
    {
        Bytes record;
        record.name20(preset.name);
        record.u16((juce::uint16) preset.program); record.u16((juce::uint16) preset.bank);
        record.u16((juce::uint16) presetBagIndex);
        record.u32(0); record.u32(0); record.u32(0);
        phdr.v.insert(phdr.v.end(), record.v.begin(), record.v.end());
        presetBagIndex += (int) preset.parts.size();
    }
    Bytes phdrTerminal; phdrTerminal.name20("EOP"); phdrTerminal.u16(0); phdrTerminal.u16(0);
    phdrTerminal.u16((juce::uint16) presetBagIndex); phdrTerminal.u32(0); phdrTerminal.u32(0); phdrTerminal.u32(0);
    phdr.v.insert(phdr.v.end(), phdrTerminal.v.begin(), phdrTerminal.v.end());
    chunk(pdta, "phdr", phdr); chunk(pdta, "pbag", pbag); chunk(pdta, "pgen", pgen);
    chunk(pdta, "inst", inst); chunk(pdta, "ibag", ibag); chunk(pdta, "igen", igen);
    chunk(pdta, "shdr", shdr);

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
    auto path = t.dir.getChildFile("samples.sf2");
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
}
