#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/rex/Rex2Decoder.h"
#include "core/rex/Rex2SlicePlayer.h"
#include "core/audio/AudioEngine.h"
#include <cmath>
#include <iostream>
using namespace sonicator;
TEST_CASE("REX MIDI boost does not boost whole loop audition", "[.real-rex-gain]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_RX2", {});
    REQUIRE(path.isNotEmpty());
    Rex2SlicePlayer rex, reference, auditionReference; juce::String error;
    REQUIRE(rex.load(juce::File(path), 36, error));
    REQUIRE(reference.load(juce::File(path), 36, error));
    REQUIRE(auditionReference.load(juce::File(path), 36, error));
    juce::AudioTransportSource transport; MidiSampler sampler; Sf2KitSampler kit;
    SampleAudition audition; ExternalKitSampler external;
    PreviewOutput output(transport, sampler, kit, rex, audition, external);
    output.prepareToPlay(128, 44100); output.midi = true; output.rexMode = true; output.gain = 0.5f;
    reference.prepare(44100); auditionReference.prepare(44100);
    juce::AudioBuffer<float> midiOnly(2, 4096), auditionOnly(2, 4096), mixed(2, 4096);
    juce::MidiBuffer midi, empty;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.5f), 0);
    reference.render(midiOnly, 0, 4096, midi);
    auditionReference.startAudition(false);
    auditionReference.render(auditionOnly, 0, 4096, empty);
    rex.startAudition(false);
    output.collector.push(juce::MidiMessage::noteOn(1, 36, 0.5f));
    output.getNextAudioBlock({ &mixed, 0, 4096 });
    REQUIRE(midiOnly.getMagnitude(0, 4096) > 0.001f);
    REQUIRE(auditionOnly.getMagnitude(0, 4096) > 0.001f);
    float maxError = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4096; ++i)
            maxError = juce::jmax(maxError, std::abs(mixed.getSample(ch, i)
                - (midiOnly.getSample(ch, i) * 1.2f + auditionOnly.getSample(ch, i)) * 0.5f));
    REQUIRE(maxError < 0.00001f);
    output.collector.push(juce::MidiMessage::allSoundOff(1));
    output.getNextAudioBlock({ &mixed, 0, 128 });
    REQUIRE(rex.activeVoices() == 0);
    REQUIRE(rex.auditionPlaying());
}
TEST_CASE("REX MIDI voices advance at device rates above the source rate", "[.real-rex-rate]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_RX2", {});
    REQUIRE(path.isNotEmpty());
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO("device rate " << rate);
        Rex2SlicePlayer player;
        juce::String error;
        REQUIRE(player.load(juce::File(path), 36, error));
        const auto starts = player.sliceStarts();
        const auto audio = player.audio();
        REQUIRE(! starts.empty());
        player.prepare(rate);
        player.setGateMode(false);
        juce::AudioBuffer<float> buffer(2, 64);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
        player.render(buffer, 0, 64, midi);
        const double expected = starts.front() + 64.0 * audio->sampleRate / rate;
        REQUIRE(std::abs(player.firstVoicePosition() - expected) < 0.001);
        midi.clear();
        const int blocks = (int) std::ceil(player.lengthSeconds() * rate / 64.0) + 2;
        for (int i = 0; i < blocks; ++i) player.render(buffer, 0, 64, midi);
        REQUIRE(player.activeVoices() == 0);
        REQUIRE(player.activeSlices().empty());
    }
}
// Opt-in: decodes a real REX2 file supplied through SONICATOR_RX2.
TEST_CASE("A real REX2 file decodes to finite audio", "[.real-rex]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_RX2", {});
    REQUIRE(path.isNotEmpty());
    juce::Array<juce::File> files;
    const juce::File root(path);
    if (root.isDirectory())
        root.findChildFiles(files, juce::File::findFiles, true, "*.rx2");
    else
        files.add(root);
    REQUIRE(! files.isEmpty());
    int decoded = 0, failed = 0;
    std::vector<float> peaks;
    if (files.size() > 1)
    {
        for (int i = 0; i < files.size() && decoded + failed < 60; ++i)
        {
            Rex2Audio probe; juce::String probeError;
            if (decodeRex2Audio(files[i], probe, probeError))
            {
                ++decoded;
                float peak = 0; for (float v : probe.left) peak = juce::jmax(peak, std::abs(v));
                peaks.push_back(peak);
                if (peak < 0.001f) std::cout << "rex silent: " << files[i].getFullPathName() << " peak=" << peak << std::endl;
            }
            else
            {
                ++failed;
                std::cout << "rex failed: " << files[i].getFullPathName() << " :: " << probeError << std::endl;
            }
        }
        float minimum = 2, maximum = 0;
        for (float p : peaks) { minimum = juce::jmin(minimum, p); maximum = juce::jmax(maximum, p); }
        std::cout << "rex batch: decoded=" << decoded << " failed=" << failed
                  << " peak_min=" << minimum << " peak_max=" << maximum << std::endl;
        REQUIRE(decoded > 0);
        REQUIRE(failed == 0);
        return;
    }
    Rex2Audio audio; juce::String error;
    const bool ok = decodeRex2Audio(juce::File(path), audio, error);
    INFO(path + " :: " + error);
    REQUIRE(ok);
    REQUIRE(audio.frames > 0);
    REQUIRE(audio.sampleRate > 0);
    REQUIRE(audio.left.size() == (size_t) audio.frames);
    float peak = 0;
    bool finite = true;
    for (float value : audio.left)
    {
        if (! std::isfinite(value)) { finite = false; break; }
        peak = juce::jmax(peak, std::abs(value));
    }
    REQUIRE(finite);
    std::cout << "rex real: frames=" << audio.frames << " rate=" << audio.sampleRate
              << " channels=" << audio.channels << " depth=" << audio.bitDepth
              << " peak=" << peak << std::endl;
    REQUIRE(peak > 0.001f);
    REQUIRE(peak <= 1.0f);
}

TEST_CASE("REX2 slices respond to different notes and to NoteOff", "[.real-rex-player]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_RX2", {});
    REQUIRE(path.isNotEmpty());
    juce::File file(path);
    if (file.isDirectory())
    {
        juce::Array<juce::File> files;
        file.findChildFiles(files, juce::File::findFiles, true, "*.rx2");
        REQUIRE(! files.isEmpty());
        file = files[0];
    }
    Rex2SlicePlayer player; juce::String error;
    REQUIRE(player.load(file, 36, error));
    REQUIRE(player.sliceCount() > 0);
    // A marker-only first SLCE must retain MIDI note 36 after load, not disappear.
    const auto metadata = extractRex2Metadata(file);
    const auto playable = playableRex2Slices(metadata.info.slices, metadata.info.frames);
    REQUIRE(! playable.empty());
    REQUIRE(player.sliceStarts().front() == playable.front().start);
    player.prepare(44100);
    juce::AudioBuffer<float> first(2, 4096), second(2, 4096);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    player.render(first, 0, 4096, midi);
    const float firstPeak = first.getMagnitude(0, 4096);
    REQUIRE(firstPeak > 0.001f);
    REQUIRE(! player.activeSlices().empty());
    REQUIRE(player.activeSlices().front() == 0);
    if (player.sliceCount() > 1)
    {
        player.reset();
        juce::MidiBuffer up;
        up.addEvent(juce::MidiMessage::noteOn(1, 37, 1.0f), 0);
        player.render(second, 0, 4096, up);
        float difference = 0;
        for (int i = 0; i < 2048; ++i) difference += std::abs(first.getSample(0, i) - second.getSample(0, i));
        REQUIRE(difference > 0.01f); // different slices, not the whole loop transposed
    }
    player.reset();
    juce::AudioBuffer<float> gated(2, 4096);
    juce::MidiBuffer gateOff;
    gateOff.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    gateOff.addEvent(juce::MidiMessage::noteOff(1, 36), 512);
    player.setGateMode(true);
    player.render(gated, 0, 4096, gateOff);
    REQUIRE(player.activeVoices() == 0);
    player.reset();
    player.setGateMode(false);
    juce::AudioBuffer<float> oneShot(2, 4096);
    juce::MidiBuffer oneShotOff;
    oneShotOff.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    oneShotOff.addEvent(juce::MidiMessage::noteOff(1, 36), 512);
    player.render(oneShot, 0, 4096, oneShotOff);
    REQUIRE(player.activeVoices() == 1);
    AudioEngine engine(false);
    REQUIRE(engine.previewRex2(file));
    REQUIRE(engine.isRex2Mode());
    REQUIRE(engine.rexSliceCount() == player.sliceCount());
    REQUIRE(engine.rexGateMode());
    engine.setRexGateMode(false);
    REQUIRE_FALSE(engine.rexGateMode());
    engine.preparePreviewBlock(512);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    juce::AudioBuffer<float> rendered(2, 512);
    engine.renderPreviewBlock(rendered, 512);
    REQUIRE(rendered.getMagnitude(0, 512) > 0.001f);
    // The exact PreviewOutput path: collector -> MIDI buffer -> REX2 slice mix.
    struct Silent final : juce::PositionableAudioSource
    {
        juce::int64 position = 0;
        void prepareToPlay(int,double) override {}
        void releaseResources() override {}
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override { info.clearActiveBufferRegion(); }
        void setNextReadPosition(juce::int64 p) override { position = p; }
        juce::int64 getNextReadPosition() const override { return position; }
        juce::int64 getTotalLength() const override { return 0; }
        bool isLooping() const override { return false; }
    } silent;
    juce::AudioTransportSource transport; transport.setSource(&silent);
    MidiSampler ordinary; Sf2KitSampler sf2; Rex2SlicePlayer slices;
    REQUIRE(slices.load(file, 36, error));
    SampleAudition auditionSource; ExternalKitSampler externalSource; PreviewOutput output(transport, ordinary, sf2, slices, auditionSource, externalSource);
    output.prepareToPlay(512, 44100);
    output.midi = true; output.rexMode = true;
    output.collector.push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    juce::AudioBuffer<float> mixed(2, 512);
    output.getNextAudioBlock({ &mixed, 0, 512 });
    REQUIRE(mixed.getMagnitude(0, 512) > 0.001f);
    output.releaseResources(); transport.setSource(nullptr);
    // Whole-loop audition used by Play: sequential playback, loop and stop.
    player.reset();
    player.prepare(44100);
    player.startAudition(false);
    juce::AudioBuffer<float> audition(2, 4096);
    juce::MidiBuffer empty;
    player.render(audition, 0, 4096, empty);
    REQUIRE(audition.getMagnitude(0, 4096) > 0.001f);
    REQUIRE(player.auditionSeconds() > 0.0);
    player.startAudition(true);
    player.seekAudition(player.lengthSeconds() - 0.01);
    juce::AudioBuffer<float> looped(2, 4096);
    player.render(looped, 0, 4096, empty);
    REQUIRE(player.auditionPlaying());
    player.stopAudition();
    juce::AudioBuffer<float> stopped(2, 1024);
    player.render(stopped, 0, 1024, empty);
    REQUIRE_FALSE(player.auditionPlaying());
}
