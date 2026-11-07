#include <catch2/catch_test_macros.hpp>
#include "core/sf2/Sf2Bank.h"
#include "core/sf2/Sf2KitSampler.h"
#include "core/audio/AudioEngine.h"
#include <iostream>
using namespace sonicator;
// Opt-in: validates the parser and sampler on a real SoundFont supplied by the user.
TEST_CASE("A real SF2 drum bank renders audible audio", "[.real-sf2]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_SF2", {});
    REQUIRE(path.isNotEmpty());
    Sf2Bank bank; juce::String error;
    INFO(path + " :: " + error);
    REQUIRE(bank.load(juce::File(path), error));
    const int drum = bank.firstDrumPreset();
    REQUIRE(drum >= 0);
    const auto& preset = bank.presets()[(size_t) drum];
    REQUIRE(! preset.zones.empty());
    Sf2KitSampler sampler;
    REQUIRE(sampler.load(bank, drum, error));
    sampler.prepare(44100);
    const int note = preset.zones.front().keyLow;
    juce::AudioBuffer<float> buffer(2, 8192);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    sampler.render(buffer, 0, 8192, midi);
    const float peak = buffer.getMagnitude(0, 8192);
    std::cout << "sf2 real: bank_presets=" << bank.presets().size()
              << " drum_preset='" << preset.name << "' zones=" << preset.zones.size()
              << " note=" << note << " peak=" << peak
              << " samples=" << bank.samples().size() << std::endl;
    REQUIRE(peak > 0.001f);
    // The engine path: load the bank, then switch through every preset (drums and melodic).
    AudioEngine engine(false);
    REQUIRE(engine.previewSoundFont(juce::File(path)));
    REQUIRE(engine.isSoundFontMode());
    REQUIRE(engine.soundFontPresetCount() == (int) bank.presets().size());
    for (int i = 0; i < engine.soundFontPresetCount(); ++i)
        REQUIRE(engine.selectSoundFontPreset(i));
    REQUIRE(engine.selectSoundFontPreset(drum));
}
