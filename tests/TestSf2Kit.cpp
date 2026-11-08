#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/sf2/Sf2KitSampler.h"
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
void chunk(Bytes& out, const char* id, Bytes payload)
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
Bytes sampleHeader(const juce::String& name, int start, int end, int loopStart, int loopEnd, int root)
{
    Bytes b; b.name20(name); b.u32((juce::uint32) start); b.u32((juce::uint32) end);
    b.u32((juce::uint32) loopStart); b.u32((juce::uint32) loopEnd); b.u32(44100);
    b.v.push_back((juce::uint8) root); b.v.push_back(0); b.u16(0); b.u16(1);
    return b;
}
// One sample, one preset, one zone covering the whole keyboard. The sample is a 440 Hz sine.
juce::File writeSineBank(TestDirectory& t, int frames = 44100, int loopMode = 0,
                         int loopStart = 0, int loopEnd = 0)
{
    Bytes pcm;
    for (int i = 0; i < frames; ++i)
        pcm.i16((juce::int16) std::lround(0.8 * 32767.0 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0)));
    Bytes smpl; chunk(smpl, "smpl", pcm);
    Bytes sdta; sdta.ascii("sdta"); sdta.v.insert(sdta.v.end(), smpl.v.begin(), smpl.v.end());
    Bytes info; info.ascii("INFO");

    Bytes pbag; pbag.u16(0); pbag.u16(0); pbag.u16(1); pbag.u16(0);
    Bytes pgen; pgen.u16(41); pgen.u16(0);
    const int genCount = loopMode != 0 ? 3 : 2;
    Bytes ibag;
    ibag.u16(0); ibag.u16(0);
    ibag.u16((juce::uint16) genCount); ibag.u16(0);
    ibag.u16((juce::uint16) genCount); ibag.u16(0);
    Bytes igen;
    igen.u16(43); igen.u16((juce::uint16) (0 | (127 << 8)));
    igen.u16(53); igen.u16(0);
    if (loopMode != 0) { igen.u16(54); igen.u16((juce::uint16) loopMode); }
    Bytes inst; inst.name20("Kit"); inst.u16(0);
    Bytes instTerminal; instTerminal.name20("EOT"); instTerminal.u16(2);
    inst.v.insert(inst.v.end(), instTerminal.v.begin(), instTerminal.v.end());
    Bytes shdr;
    auto add = [&](Bytes b) { shdr.v.insert(shdr.v.end(), b.v.begin(), b.v.end()); };
    add(sampleHeader("sine", 0, frames, loopStart, loopEnd, 60));
    Bytes end; end.name20("EOS"); for (int i = 0; i < 26; ++i) end.v.push_back(0); add(end);

    Bytes pdta; pdta.ascii("pdta");
    Bytes phdr;
    auto addPreset = [&](Bytes b) { phdr.v.insert(phdr.v.end(), b.v.begin(), b.v.end()); };
    addPreset(presetHeader("Kit", 128, 0));
    addPreset(presetHeader("EOP", 0, 1));
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
    auto path = t.dir.getChildFile("sine.sf2");
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
float renderPeak(Sf2KitSampler& sampler, int note, float velocity, int frames, int block = 512)
{
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
    float peak = 0;
    for (int done = 0; done < frames; done += block)
    {
        sampler.render(buffer, 0, block, midi);
        midi.clear();
        peak = juce::jmax(peak, buffer.getMagnitude(0, block));
    }
    return peak;
}
int zeroCrossings(const juce::AudioBuffer<float>& buffer, int frames)
{
    int count = 0;
    for (int i = 1; i < frames; ++i)
        if ((buffer.getSample(0, i - 1) < 0) != (buffer.getSample(0, i) < 0)) ++count;
    return count;
}
}
TEST_CASE("SF2 kit sampler plays the mapped sample at the requested pitch", "[sf2][sampler]")
{
    TestDirectory t;
    auto path = writeSineBank(t);
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    Sf2KitSampler sampler;
    REQUIRE(sampler.load(bank, 0, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 8192);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 8192, midi);
    REQUIRE(buffer.getMagnitude(0, 8192) > 0.1f);
    const int low = zeroCrossings(buffer, 8192);
    sampler.reset();
    juce::AudioBuffer<float> higher(2, 8192);
    juce::MidiBuffer up;
    up.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
    sampler.render(higher, 0, 8192, up);
    const int high = zeroCrossings(higher, 8192);
    REQUIRE(high > (int) (low * 1.8));
    REQUIRE(high < (int) (low * 2.2));
}
TEST_CASE("Velocity scales a voice and NoteOff releases it", "[sf2][sampler]")
{
    TestDirectory t;
    auto path = writeSineBank(t, 44100 * 3);
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    Sf2KitSampler sampler;
    REQUIRE(sampler.load(bank, 0, error));
    sampler.prepare(44100);
    const float full = renderPeak(sampler, 60, 1.0f, 4096);
    sampler.reset();
    const float quiet = renderPeak(sampler, 60, 0.25f, 4096);
    REQUIRE(quiet < full * 0.4f);
    REQUIRE(quiet > full * 0.15f);
    sampler.reset();
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 2048);
    sampler.render(buffer, 0, 4096, off);
    REQUIRE(sampler.activeVoices() == 1); // NoteOff now leaves a two-second tail.
    REQUIRE(buffer.getMagnitude(0, 4096) > 0.01f);
    off.clear();
    for (int i = 0; i < 23; ++i) sampler.render(buffer, 0, 4096, off);
    REQUIRE(sampler.activeVoices() == 0);
}
TEST_CASE("A looping SF2 sample keeps sounding past its end", "[sf2][sampler]")
{
    TestDirectory t;
    auto path = writeSineBank(t, 2000, 1, 0, 2000);
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    Sf2KitSampler sampler;
    REQUIRE(sampler.load(bank, 0, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 4096, midi);
    REQUIRE(sampler.activeVoices() == 1);
    REQUIRE(buffer.getMagnitude(0, 4096) > 0.1f);
}
