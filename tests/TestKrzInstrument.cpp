#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/kurzweil/KrzInstrument.h"
#include "core/sampler/ExternalKitSampler.h"
#include "TestHelpers.h"
#include "KrzTestFile.h"
#include <cmath>
using namespace sonicator;
using namespace krztest;

namespace {
EntrySpec note(int sampleId, int subSample = 1, int tuning = 0, int volume = 0)
{
    EntrySpec e; e.sampleId = sampleId; e.subSample = subSample;
    e.tuningCents = tuning; e.volumeHalfDb = volume; return e;
}
KeymapSpec mapRange(int id, int sampleId, int low, int high, int method = 0x03, int entrySize = 3)
{
    KeymapSpec k; k.id = id; k.method = method; k.entrySize = entrySize;
    for (auto& level : k.levels)
    {
        level.assign(128, EntrySpec {});
        for (int key = low; key <= high && key < 128; ++key)
            level[(size_t) (key - 12)] = note(sampleId);
    }
    return k;
}
SampleSpec sineSample(int id, int rootKey, std::vector<int16_t>& pcm, int frames = 25000, double hz = 440.0)
{
    SampleSpec s; s.id = id; s.name = "Tone";
    HeaderSpec h; h.rootKey = rootKey; h.looping = true;
    h.startWord = 0; h.loopWord = 0; h.endWord = (uint32_t) (frames - 1);
    s.headers = { h };
    s.headers[0].startWord = (uint32_t) pcm.size();
    s.headers[0].endWord = (uint32_t) (pcm.size() + frames - 1);
    s.headers[0].loopWord = (uint32_t) pcm.size();
    for (int i = 0; i < frames; ++i)
        pcm.push_back((int16_t) std::lround(20000.0 * std::sin(2 * juce::MathConstants<double>::pi * hz * i / 25000.0)));
    return s;
}
int zeroCrossings(const juce::AudioBuffer<float>& buffer, int channel, int from, int count)
{
    int crossings = 0;
    for (int i = from + 1; i < from + count; ++i)
        if ((buffer.getSample(channel, i - 1) < 0.0f) != (buffer.getSample(channel, i) < 0.0f))
            ++crossings;
    return crossings;
}
} // namespace

TEST_CASE("KRZ program becomes zones with its velocity bands", "[krz][sampler]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.samples = { sineSample(200, 60, spec.pcm), sineSample(201, 60, spec.pcm) };
    KeymapSpec low = mapRange(200, 200, 36, 59);
    KeymapSpec high = mapRange(201, 201, 60, 96);
    for (int level = 0; level < 5; ++level) low.levels[(size_t) level] = low.levels[0];
    for (int level = 0; level < 8; ++level) high.levels[(size_t) level] = high.levels[0];
    low.levels[5] = low.levels[6] = low.levels[7] = {};
    spec.keymaps = { low, high };
    ProgramSpec program; program.id = 200; program.name = "Split";
    program.layers = { LayerSpec { 200, 0 }, LayerSpec { 201, 0 } };
    spec.programs = { program };
    auto file = writeBank(dir.dir, "zones.krz", spec);

    FileInstrument instrument; juce::String error;
    REQUIRE(loadKrzInstrument(file, instrument, error));
    CHECK(instrument.name == "zones");
    REQUIRE(instrument.programs.size() == 1);
    CHECK(instrument.programs[0].name == "Split");
    CHECK(instrument.programs[0].number == 200);
    CHECK(instrument.currentProgram == 0);
    std::vector<int> lowLevels, highLevels;
    for (const auto& zone : instrument.zones)
    {
        if (zone.keyLow == 36)
        {
            CHECK(zone.keyHigh == 59);
            CHECK(zone.rootKey == 60);
            CHECK((zone.velocityHigh == zone.velocityLow + 15));
            lowLevels.push_back(zone.velocityLow);
        }
        if (zone.keyLow == 60)
        {
            CHECK(zone.keyHigh == 96);
            CHECK((zone.velocityHigh == zone.velocityLow + 15 || zone.velocityHigh == 127));
            highLevels.push_back(zone.velocityLow);
        }
    }
    CHECK(lowLevels == std::vector<int> { 0, 16, 32, 48, 64 });
    CHECK(highLevels == std::vector<int> { 0, 16, 32, 48, 64, 80, 96, 112 });
}

TEST_CASE("KRZ program switch replaces the zones", "[krz][sampler]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.samples = { sineSample(200, 60, spec.pcm), sineSample(201, 72, spec.pcm) };
    spec.keymaps = { mapRange(200, 200, 36, 48), mapRange(201, 201, 72, 84) };
    ProgramSpec first; first.id = 200; first.name = "Low"; first.layers = { LayerSpec { 200, 0 } };
    ProgramSpec second; second.id = 201; second.name = "High"; second.layers = { LayerSpec { 201, 0 } };
    spec.programs = { first, second };
    auto file = writeBank(dir.dir, "switch.krz", spec);

    FileInstrument instrument; juce::String error;
    REQUIRE(loadKrzInstrument(file, instrument, error));
    REQUIRE(instrument.programs.size() == 2);
    REQUIRE_FALSE(instrument.zones.empty());
    CHECK(instrument.zones[0].keyLow == 36);
    REQUIRE(selectInstrumentProgram(instrument, 1, error));
    REQUIRE_FALSE(instrument.zones.empty());
    CHECK(instrument.zones[0].keyLow == 72);
    CHECK(instrument.currentProgram == 1);
    CHECK_FALSE(selectInstrumentProgram(instrument, 5, error));
    CHECK(error.isNotEmpty());
}

TEST_CASE("KRZ banks render at keymap pitch and stay silent outside the layer", "[krz][sampler]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.samples = { sineSample(200, 60, spec.pcm) };
    spec.keymaps = { mapRange(200, 200, 60, 72) };
    ProgramSpec program; program.id = 200; program.name = "Tone"; program.layers = { LayerSpec { 200, 0 } };
    spec.programs = { program };
    auto file = writeBank(dir.dir, "pitch.krz", spec);

    FileInstrument instrument; juce::String error;
    REQUIRE(loadKrzInstrument(file, instrument, error));
    ExternalKitSampler sampler;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
    sampler.render(buffer, 0, 4096, midi);
    const int crossings = zeroCrossings(buffer, 0, 256, 3840);
    // 440 Hz at the root, one octave up at note 72 → 880 Hz: about 153 crossings in 3840 frames.
    CHECK(crossings > 140);
    CHECK(crossings < 168);

    sampler.reset();
    juce::AudioBuffer<float> silent(2, 1024);
    juce::MidiBuffer outside;
    outside.addEvent(juce::MidiMessage::noteOn(1, 30, 1.0f), 0);
    sampler.render(silent, 0, 1024, outside);
    CHECK(silent.getMagnitude(0, 1024) < 1.0e-4f);
}

TEST_CASE("KRZ stereo sample becomes one two-channel source", "[krz][sampler]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec stereo; stereo.id = 200; stereo.name = "Stereo"; stereo.stereo = true;
    HeaderSpec left; left.rootKey = 60; left.startWord = 0; left.endWord = 3;
    HeaderSpec right = left; right.startWord = 4; right.endWord = 7;
    stereo.headers = { left, right };
    spec.samples = { stereo };
    spec.pcm = { 1000, 2000, 3000, 4000, -1000, -2000, -3000, -4000 };
    spec.keymaps = { mapRange(200, 200, 60, 72) };
    ProgramSpec program; program.id = 200; program.name = "Stereo"; program.layers = { LayerSpec { 200, 0 } };
    spec.programs = { program };
    auto file = writeBank(dir.dir, "stereo.krz", spec);

    FileInstrument instrument; juce::String error;
    REQUIRE(loadKrzInstrument(file, instrument, error));
    REQUIRE(instrument.sources.size() == 1);
    CHECK(instrument.sources[0].channels == 2);
    REQUIRE(instrument.audio.size() == 1);
    CHECK(instrument.audio[0]->channels == 2);
    CHECK(instrument.audio[0]->right[0] == Catch::Approx(-1000.0f / 32768.0f));
    REQUIRE_FALSE(instrument.zones.empty());
    CHECK(instrument.zones[0].sampleIndex == 0);
}

TEST_CASE("KRZ layer with a missing keymap is dropped and reported", "[krz][sampler]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.samples = { sineSample(200, 60, spec.pcm) };
    spec.keymaps = { mapRange(200, 200, 60, 72) };
    ProgramSpec program; program.id = 200; program.name = "Half";
    program.layers = { LayerSpec { 999, 0 }, LayerSpec { 200, 0 } };
    spec.programs = { program };
    auto file = writeBank(dir.dir, "missing.krz", spec);

    FileInstrument instrument; juce::String error;
    REQUIRE(loadKrzInstrument(file, instrument, error));
    CHECK_FALSE(instrument.zones.empty());
    CHECK(instrument.zones[0].keyLow == 60);
    CHECK(instrument.warnings.joinIntoString("|").contains("999"));
}

TEST_CASE("KRZ bank without its own samples reports the factory ROM", "[krz][sampler]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.keymaps = { mapRange(200, 0, 60, 72) };
    ProgramSpec program; program.id = 200; program.name = "Rom Only"; program.layers = { LayerSpec { 200, 0 } };
    spec.programs = { program };
    auto file = writeBank(dir.dir, "rom.krz", spec);

    FileInstrument instrument; juce::String error;
    CHECK_FALSE(loadKrzInstrument(file, instrument, error));
    CHECK(error == "This bank uses the K2000 factory ROM samples; the file contains no samples of its own");
}

TEST_CASE("KRZ bank without programs falls back to keymaps and samples", "[krz][sampler]")
{
    TestDirectory dir;
    juce::String error;

    SECTION("one program per keymap")
    {
        BankSpec spec;
        spec.samples = { sineSample(200, 60, spec.pcm), sineSample(201, 72, spec.pcm) };
        spec.keymaps = { mapRange(200, 200, 36, 48), mapRange(201, 201, 72, 84) };
        auto file = writeBank(dir.dir, "keymaps.krz", spec);
        FileInstrument instrument;
        REQUIRE(loadKrzInstrument(file, instrument, error));
        REQUIRE(instrument.programs.size() == 2);
        CHECK(instrument.programs[0].name == "Keymap");
        CHECK_FALSE(instrument.programs[0].zones.empty());
        CHECK(instrument.programs[0].zones[0].keyLow == 36);
        CHECK(instrument.programs[1].zones[0].keyLow == 72);
    }
    SECTION("one program per sample")
    {
        BankSpec spec;
        spec.samples = { sineSample(200, 60, spec.pcm) };
        auto file = writeBank(dir.dir, "samples.krz", spec);
        FileInstrument instrument;
        REQUIRE(loadKrzInstrument(file, instrument, error));
        REQUIRE(instrument.programs.size() == 1);
        REQUIRE_FALSE(instrument.programs[0].zones.empty());
        CHECK(instrument.programs[0].zones[0].keyLow == 0);
        CHECK(instrument.programs[0].zones[0].keyHigh == 127);
        CHECK(instrument.programs[0].zones[0].rootKey == 60);
    }
}
