#include <catch2/catch_test_macros.hpp>
#include "core/sampler/ExternalKitSampler.h"
#include "core/sampler/FileInstrument.h"
#include "core/audio/AudioEngine.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("IK KICKS MIDI follow and pinned selection leave overlapping hits sounding", "[.real][sampler][nki]")
{
    const juce::File file("/Volumes/1TBSSD/samples/Inland.Knights.Deep.Dub.Tech.House/IK_SAMPLER_PATCHES/KONTAKT/IK_DRUM_HITS/IK_KICKS.nki");
    if (! file.existsAsFile()) SKIP("Real library is not mounted");
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    engine.preparePreviewBlock(256);
    juce::AudioBuffer<float> buffer(2, 256);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 60, 1.0f));
    engine.renderPreviewBlock(buffer, 256);
    preview.refreshMidiFollow();
    REQUIRE(engine.instrumentActiveVoices() == 1);
    const auto firstSource = preview.selectedInstrumentSampleId();
    engine.midiCollector().push(juce::MidiMessage::noteOff(1, 60));
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 61, 1.0f));
    engine.renderPreviewBlock(buffer, 256);
    preview.refreshMidiFollow();
    REQUIRE(engine.instrumentActiveVoices() == 2);
    REQUIRE(preview.selectedInstrumentSampleId() != firstSource);
    preview.selectRange(0.01, 0.03);
    const auto pinned = preview.selectedInstrumentSampleId();
    engine.midiCollector().push(juce::MidiMessage::noteOff(1, 61));
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 60, 1.0f));
    engine.renderPreviewBlock(buffer, 256);
    preview.refreshMidiFollow();
    REQUIRE(engine.instrumentActiveVoices() == 3);
    REQUIRE(preview.selectedInstrumentSampleId() == pinned);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    preview.clearSelection(); preview.refreshMidiFollow();
    REQUIRE(preview.selectedInstrumentSampleId() == firstSource);
    REQUIRE(engine.instrumentActiveVoices() == 3);
    REQUIRE_FALSE(engine.isPlaying());
    engine.stop();
    REQUIRE(engine.instrumentActiveVoices() == 0);
}
namespace {
const auto realRoot = juce::File("/Volumes/1TBSSD/samples/Samplephonics - Dub Techno Progressions");
}
TEST_CASE("Real EXS24 kit resolves its samples read-only", "[.real][sampler][exs]")
{
    const auto file = realRoot.getChildFile("Sampler Instruments/EXS24/Kicks.exs");
    if (! file.existsAsFile()) SKIP("Real library is not mounted");
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::exs24, instrument, error));
    INFO(error);
    REQUIRE(instrument.sources.size() == 38);
    REQUIRE(instrument.zones.size() == 38);
    for (const auto& source : instrument.sources) REQUIRE(source.isPlayable());
    bool found = false;
    for (const auto& source : instrument.sources) found = found || source.name == "Kick_01.wav";
    REQUIRE(found);
    REQUIRE(instrument.sources[0].frameCount > 1000);
}
TEST_CASE("Real newer EXS24 variant keeps its sample reference", "[.real][sampler][exs]")
{
    const auto file = juce::File("/Volumes/1TBSSD/samples/Dub and Warehouse Techno/ss_dwt_sampler instruments/EXS24/SS_DWT_Chord Stab 03.exs");
    if (! file.existsAsFile()) SKIP("Real library is not mounted");
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::exs24, instrument, error));
    INFO(error);
    REQUIRE(instrument.sources.size() == 1);
    REQUIRE(instrument.zones.size() == 1);
    REQUIRE(instrument.sources[0].name == "SS_DWT_CHORD_STABS_003_Am.wav");
    REQUIRE(instrument.sources[0].isPlayable());
}
TEST_CASE("Real NKI XML kit resolves encoded relative paths", "[.real][sampler][nki]")
{
    const auto file = realRoot.getChildFile("Sampler Instruments/Kontakt/Kicks.nki");
    if (! file.existsAsFile()) SKIP("Real library is not mounted");
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::nki, instrument, error));
    INFO(error);
    REQUIRE(instrument.sources.size() == 38);
    REQUIRE(instrument.zones.size() == 38);
    for (const auto& source : instrument.sources) REQUIRE(source.isPlayable());
    REQUIRE(instrument.sources[0].name == "Kick_01.wav");
}
TEST_CASE("Real NiSS drum-map NKI renders a pitched note", "[.real][sampler][nki]")
{
    const auto file = juce::File("/Volumes/1TBSSD/samples/!__OLDSCHOOL/X-Static Goldmine 1/12 LOP+B.LOP.nki");
    if (! file.existsAsFile()) SKIP("Real library is not mounted");
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::nki, instrument, error));
    INFO(error);
    REQUIRE(instrument.zones.size() == 7);
    for (const auto& source : instrument.sources) REQUIRE(source.isPlayable());
    ExternalKitSampler sampler;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 44100);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    sampler.render(buffer, 0, 44100, midi);
    REQUIRE(sampler.activeVoices() >= 1);
    REQUIRE(buffer.getMagnitude(0, 44100) > 0.05f);
}
TEST_CASE("Real EXS24 block size variants load", "[.real][sampler][exs]")
{
    const juce::StringArray paths {
        "/Volumes/1TBSSD/samples/Mutekki Media - FX Revolution Vol.1/SOR FX Revolution Vol.1 (EXS Instruments)/SOR FXR1 Sweeps/SOR FXR1 Sweeps 03.exs",
        "/Volumes/1TBSSD/samples/!__OLDSCHOOL/Best Service ProSamples vol.36 - Chillout [AIFF, EXS24, HALion, WAV] 1CD/PS-36 EXS24 Chillout/Sampler Instruments/PS-36 EXS24 Chillout/Glass Atmos/Singing Glass.exs",
        "/Volumes/1TBSSD/samples/Samplephonics - Dub Techno Progressions/Sampler Instruments/EXS24/Kicks.exs",
        "/Volumes/1TBSSD/samples/Dub and Warehouse Techno/ss_dwt_sampler instruments/EXS24/SS_DWT_Chord Stab 03.exs"
    };
    for (const auto& path : paths)
    {
        const juce::File file(path);
        if (! file.existsAsFile()) SKIP("Real library is not mounted");
        FileInstrument instrument; juce::String error;
        INFO(path);
        REQUIRE(loadFileInstrument(file, FileInstrumentFormat::exs24, instrument, error));
        REQUIRE_FALSE(instrument.zones.empty());
        bool playable = false;
        for (const auto& source : instrument.sources) playable = playable || source.isPlayable();
        REQUIRE(playable);
    }
}
TEST_CASE("Real EXS24 zone plays a mapped MIDI note", "[.real][sampler][exs]")
{
    const auto file = juce::File("/Volumes/1TBSSD/samples/E-MU Emax (EXS24, AKAI)/Original Factory Samples and Presets E-MU Emax (EXS24, AKAI).part1.rar Folder/exs24/BROS__QUAY/ Greenhouse.exs");
    if (! file.existsAsFile()) SKIP("Real library is not mounted");
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::exs24, instrument, error));
    INFO(error);
    REQUIRE(instrument.zones.size() == 2);
    ExternalKitSampler sampler;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    sampler.render(buffer, 0, 4096, midi);
    REQUIRE(sampler.activeVoices() >= 1);
    REQUIRE(buffer.getMagnitude(0, 4096) > 0.01f);
}
TEST_CASE("Real NKI drum map rings out after NoteOff", "[.real][sampler][nki]")
{
    const auto file = juce::File("/Volumes/1TBSSD/samples/Inland.Knights.Deep.Dub.Tech.House/IK_SAMPLER_PATCHES/KONTAKT/IK_DRUM_HITS/IK_KICKS.nki");
    if (! file.existsAsFile()) SKIP("Real library is not mounted");
    FileInstrument instrument; juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::nki, instrument, error));
    INFO(error);
    REQUIRE(instrument.zones.size() == 36);
    bool oneShots = true;
    for (const auto& zone : instrument.zones) oneShots = oneShots && zone.oneShot;
    REQUIRE(oneShots);
    ExternalKitSampler sampler;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 44100);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 2205);
    sampler.render(buffer, 0, 44100, midi);
    REQUIRE(buffer.getMagnitude(0, 44100) > 0.01f);
    REQUIRE(buffer.getMagnitude(4410, 4410) > 0.001f);
}
