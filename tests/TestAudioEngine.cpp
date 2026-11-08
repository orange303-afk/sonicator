#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/audio/MidiSampler.h"
#include "Sf2TestBank.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Oversized output callbacks chunk the transport as well as MIDI", "[audio][auto-preview]")
{
    struct Probe final : juce::PositionableAudioSource
    {
        int largest = 0, calls = 0; juce::int64 position = 0;
        void prepareToPlay(int,double) override {} void releaseResources() override {}
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
        { largest = juce::jmax(largest,info.numSamples); ++calls; position += info.numSamples; info.clearActiveBufferRegion(); }
        void setNextReadPosition(juce::int64 p) override { position = p; }
        juce::int64 getNextReadPosition() const override { return position; }
        juce::int64 getTotalLength() const override { return 100000; }
        bool isLooping() const override { return false; }
    } probe;
    juce::AudioTransportSource transport; transport.setSource(&probe);
    MidiSampler sampler; Sf2KitSampler kit; Rex2SlicePlayer rex; SampleAudition audition; ExternalKitSampler external; PreviewOutput output(transport,sampler,kit,rex,audition,external); output.prepareToPlay(64,44100);
    transport.start(); juce::AudioBuffer<float> buffer(2,600);
    buffer.setSample(0,0,0.123f); output.getNextAudioBlock({&buffer,16,512});
    REQUIRE(probe.largest <= 64); REQUIRE(probe.calls == 8); REQUIRE(probe.position == 512);
    REQUIRE(buffer.getSample(0,0) == 0.123f);
    output.releaseResources(); transport.setSource(nullptr);
}
TEST_CASE("MIDI output mixes ordinary preview instead of silencing it", "[audio][auto-preview]")
{
    TestDirectory t; auto file = t.wave();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    juce::AudioFormatReaderSource reader(formats.createReaderFor(file),true);
    juce::AudioTransportSource transport; transport.setSource(&reader,0,nullptr,44100);
    MidiSampler sampler; Sf2KitSampler kit; Rex2SlicePlayer rex; SampleAudition audition; ExternalKitSampler external; REQUIRE(sampler.load(file).isEmpty());
    PreviewOutput output(transport,sampler,kit,rex,audition,external); output.prepareToPlay(128,44100); output.midi = true;
    transport.start(); juce::AudioBuffer<float> buffer(2,512);
    output.getNextAudioBlock({&buffer,0,512});
    REQUIRE(buffer.getMagnitude(0,512) > 0.5f);
    REQUIRE(transport.getCurrentPosition() > 0.01);
    output.collector.push(juce::MidiMessage::noteOn(1,72,1.0f));
    output.getNextAudioBlock({&buffer,0,512}); REQUIRE(sampler.activeVoices() == 1);
    REQUIRE(transport.getCurrentPosition() > 0.02);
    output.releaseResources(); transport.setSource(nullptr);
}
TEST_CASE("MIDI input rejects SysEx and bounds overflow without stuck notes", "[audio][midi]")
{
    MidiEventQueue queue; juce::MidiBuffer midi; midi.ensureSize(65536);
    std::vector<juce::uint8> payload(20000,1);
    queue.push(juce::MidiMessage::createSysExMessage(payload.data(),(int) payload.size()));
    REQUIRE_FALSE(queue.drain(midi)); REQUIRE(midi.isEmpty());
    for (int i = 0; i < MidiEventQueue::capacity; ++i) queue.push(juce::MidiMessage::noteOn(1,60,1.0f));
    REQUIRE_FALSE(queue.drain(midi)); REQUIRE(midi.getNumEvents() == MidiEventQueue::capacity);
    midi.clear();
    for (int i = 0; i <= MidiEventQueue::capacity; ++i) queue.push(juce::MidiMessage::noteOn(1,60,1.0f));
    REQUIRE(queue.drain(midi)); REQUIRE(midi.isEmpty());
}
TEST_CASE("MIDI sampler renders polyphony with short attack and release", "[audio][midi]")
{
    TestDirectory t; MidiSampler sampler;
    REQUIRE(sampler.load(t.wave("long.wav",440,1,44100*3)).isEmpty());
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2,512);
    juce::MidiBuffer notes; notes.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),0);
    sampler.render(buffer,0,512,notes);
    REQUIRE(buffer.getSample(0,0) == 0.0f);
    REQUIRE(buffer.getMagnitude(0,512) > 0.1f);
    REQUIRE(sampler.activeVoices() == 1);
    notes.clear(); notes.addEvent(juce::MidiMessage::noteOn(1,67,0.8f),0);
    sampler.render(buffer,0,512,notes);
    REQUIRE(sampler.activeVoices() == 2);
    notes.clear(); notes.addEvent(juce::MidiMessage::noteOff(1,60),0); notes.addEvent(juce::MidiMessage::noteOff(1,67),0);
    sampler.render(buffer,0,512,notes);
    REQUIRE(sampler.activeVoices() == 2);
    notes.clear(); sampler.render(buffer,0,512,notes);
    REQUIRE(sampler.activeVoices() == 0);
}
TEST_CASE("Skip Silence detects a frame before the first nonzero sample", "[audio][silence]")
{
    TestDirectory t; auto file = t.wave("silence.wav",0,2,44100);
    // Rewrite a single frame of the second channel after 1000 silent frames.
    { juce::FileOutputStream out(file); REQUIRE(out.setPosition(44+1000*4+2)); out.writeShort(1000); }
    MidiSampler sampler; REQUIRE(sampler.load(file).isEmpty());
    REQUIRE(sampler.silenceStart() == 999);
    AudioEngine engine(false); REQUIRE(engine.loadFile(file)); engine.setSkipSilence(true); engine.play();
    REQUIRE(engine.position() == Catch::Approx(999.0/44100).margin(0.0001));
}
TEST_CASE("Non looping preview reaches EOF without automatically restarting", "[audio][regression]")
{
    TestDirectory t; AudioEngine engine(false); REQUIRE(engine.loadFile(t.wave("short.wav",440,1,4096)));
    engine.transport().prepareToPlay(256,44100); engine.play();
    juce::AudioBuffer<float> buffer(2,256);
    for (int i = 0; i < 40; ++i) engine.transport().getNextAudioBlock({&buffer,0,256});
    REQUIRE_FALSE(engine.isLooping()); REQUIRE_FALSE(engine.isPlaying());
    REQUIRE(buffer.getMagnitude(0,256) == 0);
    engine.transport().releaseResources();
}
TEST_CASE("MIDI pitch is chromatic and Skip Silence affects the next voice only", "[audio][midi]")
{
    TestDirectory t; MidiSampler sampler; REQUIRE(sampler.load(t.wave("tone.wav",440,1,44100*3)).isEmpty());
    sampler.prepare(44100); juce::AudioBuffer<float> block(2,4096);
    auto crossings = [&](int note)
    {
        sampler.reset(); juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1,note,1.0f),0);
        sampler.render(block,0,4096,midi);
        int n = 0; for (int i = 513; i < 4096; ++i)
            if (block.getSample(0,i-1) <= 0 && block.getSample(0,i) > 0) ++n;
        return n;
    };
    const int base = crossings(60), octave = crossings(72);
    REQUIRE(base > 30); REQUIRE(octave >= base*2-2); REQUIRE(octave <= base*2+2);
    auto file = t.wave("leading.wav",0,1,44100);
    { juce::FileOutputStream out(file); REQUIRE(out.setPosition(44+2000*2));
      for (int i = 0; i < 2048; ++i) out.writeShort(10000); }
    REQUIRE(sampler.load(file).isEmpty()); sampler.prepare(44100);
    juce::MidiBuffer note; note.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),0);
    sampler.render(block,0,256,note); REQUIRE(block.getMagnitude(0,256) == 0);
    sampler.skipSilence = true;
    note.clear(); sampler.render(block,0,256,note); REQUIRE(block.getMagnitude(0,256) == 0);
    note.addEvent(juce::MidiMessage::noteOn(1,64,1.0f),0);
    sampler.render(block,0,256,note); REQUIRE(block.getMagnitude(0,256) > 0.05f);
}
TEST_CASE("MIDI voices are bounded and the output honours buffer offsets", "[audio][midi]")
{
    TestDirectory t; MidiSampler sampler; REQUIRE(sampler.load(t.wave()).isEmpty()); sampler.prepare(48000);
    juce::AudioBuffer<float> buffer(2,1024); juce::MidiBuffer midi;
    for (int i = 0; i < 20; ++i) midi.addEvent(juce::MidiMessage::noteOn(1,40+i,1.0f),0);
    buffer.clear(); buffer.setSample(0,0,0.123f); sampler.render(buffer,128,512,midi);
    REQUIRE(buffer.getSample(0,0) == 0.123f); REQUIRE(sampler.activeVoices() == 16);
    midi.clear(); midi.addEvent(juce::MidiMessage::allNotesOff(1),0);
    sampler.render(buffer,128,512,midi); midi.clear(); sampler.render(buffer,128,512,midi);
    REQUIRE(sampler.activeVoices() == 0);
}
TEST_CASE("Silent and oversized samples have explicit MIDI behaviour", "[audio][midi]")
{
    TestDirectory t; MidiSampler sampler;
    REQUIRE(sampler.load(t.wave("silent.wav",0)).isEmpty()); REQUIRE(sampler.silenceStart() == 0);
    sampler.skipSilence = true; sampler.loop = true; sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2,256); juce::MidiBuffer note;
    note.addEvent(juce::MidiMessage::noteOn(1,60,1.0f),0);
    sampler.render(buffer,0,256,note); REQUIRE(buffer.getMagnitude(0,256) == 0);
    auto large = t.dir.getChildFile("too-large.wav");
    constexpr int bytes = 80*1024*1024; // 160 MiB decoded stereo float.
    {
        auto out = large.createOutputStream(); REQUIRE(out != nullptr);
        out->write("RIFF",4); out->writeInt(bytes+36); out->write("WAVEfmt ",8);
        out->writeInt(16); out->writeShort(1); out->writeShort(2); out->writeInt(44100);
        out->writeInt(176400); out->writeShort(4); out->writeShort(16);
        out->write("data",4); out->writeInt(bytes);
        REQUIRE(out->setPosition((int64_t) bytes+43)); out->writeByte(0);
    }
    REQUIRE(sampler.load(large).contains("128 MiB")); REQUIRE(sampler.activeVoices() == 0);
}
TEST_CASE("Audio preview actually renders, resamples, loops and stops", "[audio]")
{
    TestDirectory t;
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(t.wave()));
    REQUIRE_FALSE(engine.isPlaying());
    engine.transport().prepareToPlay(256, 48000);
    REQUIRE(engine.length() == Catch::Approx(1.0).margin(0.001));
    engine.setVolume(0.5f);
    engine.play();
    juce::AudioBuffer<float> buffer(2,256);
    for (int i = 0; i < 4; ++i) engine.transport().getNextAudioBlock({&buffer,0,256});
    REQUIRE(buffer.getMagnitude(0,256) > 0.3f);
    REQUIRE(buffer.getMagnitude(0,256) < 0.41f);
    REQUIRE(engine.position() > 0.01);
    engine.seek(-10); REQUIRE(engine.position() == 0);
    engine.seek(999); REQUIRE(engine.position() == Catch::Approx(1).margin(0.001));
    engine.setLooping(true); engine.seek(0.99); engine.play();
    for (int i = 0; i < 8; ++i) engine.transport().getNextAudioBlock({&buffer,0,256});
    REQUIRE(engine.isPlaying());
    REQUIRE(engine.position() < 0.1);
    engine.stop();
    REQUIRE_FALSE(engine.isPlaying()); REQUIRE(engine.position() == 0);
    engine.setVolume(-1); REQUIRE(engine.getVolume() == 0);
    engine.setVolume(5); REQUIRE(engine.getVolume() == 1);
    REQUIRE_FALSE(engine.loadFile(t.dir.getChildFile("absent.wav")));
    REQUIRE(engine.error().isNotEmpty());
    REQUIRE(engine.length() == 0);
}

TEST_CASE("MIDI preview gain is 20 percent louder without boosting audition", "[audio][midi-gain]")
{
    TestDirectory t;
    const auto file = t.wave("tone.wav", 220, 1, 44100);
    const auto bankFile = sf2test::writeBank(t, std::vector<juce::int16>(44100, 8192),
        { { "tone", 0, 44100, 0, 0, 44100 } },
        { { "Instrument", { { 60, 60, 0, 127, 0 } } } }, { { "Preset", 0, 0, { { 0 } } } });
    for (int route = 0; route < 3; ++route)
    {
        INFO("MIDI route " << route);
        juce::AudioTransportSource transport;
        MidiSampler sampler; Sf2KitSampler kit; Rex2SlicePlayer rex; SampleAudition audition; ExternalKitSampler external;
        REQUIRE(sampler.load(file).isEmpty());
        Sf2Bank bank; juce::String error;
        REQUIRE(bank.load(bankFile, error)); REQUIRE(kit.load(bank, 0, error));
        SampleAudio audio;
        audio.sampleRate = 44100; audio.channels = 1; audio.frames = 44100; audio.left.assign(44100, 0.1f);
        const auto held = std::make_shared<const SampleAudio>(audio);
        FileInstrument instrument;
        SampleDescriptor descriptor; descriptor.id = 0; descriptor.frameCount = audio.frames;
        descriptor.sampleRate = audio.sampleRate; descriptor.channels = 1;
        instrument.sources.push_back(descriptor); instrument.audio.push_back(held);
        ExternalZone zone; zone.sampleIndex = 0; zone.end = audio.frames; instrument.zones.push_back(zone);
        REQUIRE(external.load(instrument, error));
        PreviewOutput output(transport, sampler, kit, rex, audition, external);
        output.prepareToPlay(128, 44100);
        output.midi = true; output.sf2Mode = route == 1; output.externalMode = route == 2; output.gain = 0.5f;
        juce::AudioBuffer<float> reference(2, 512), actual(2, 544);
        juce::MidiBuffer notes; notes.addEvent(juce::MidiMessage::noteOn(1, 60, 0.5f), 0);
        if (route == 0) sampler.render(reference, 0, 512, notes);
        else if (route == 1) kit.render(reference, 0, 512, notes);
        else external.render(reference, 0, 512, notes);
        output.prepareToPlay(128, 44100);
        audition.setAudio(held); audition.play(false);
        actual.clear(); actual.setSample(0, 0, 0.123f);
        output.collector.push(juce::MidiMessage::controllerEvent(1, 64, 127));
        output.collector.push(juce::MidiMessage::noteOn(1, 60, 0.5f));
        output.getNextAudioBlock({ &actual, 16, 512 });
        REQUIRE(actual.getSample(0, 0) == 0.123f);
        for (int i : { 100, 200, 400, 511 })
            REQUIRE(actual.getSample(0, 16 + i)
                == Catch::Approx((reference.getSample(0, i) * 1.2f + 0.1f) * 0.5f).margin(0.00001f));
        output.prepareToPlay(128, 44100);
        audition.setAudio(held); audition.play(false);
        output.getNextAudioBlock({ &actual, 16, 512 });
        REQUIRE(actual.getSample(0, 116) == Catch::Approx(0.05f).margin(0.00001f));
        output.collector.push(juce::MidiMessage::controllerEvent(1, 64, 127));
        output.collector.push(juce::MidiMessage::noteOn(1, 60, 0.5f));
        output.getNextAudioBlock({ &actual, 16, 512 });
        for (int i = 0; i <= MidiEventQueue::capacity; ++i)
            output.collector.push(juce::MidiMessage::noteOn(1, 60, 0.5f));
        output.getNextAudioBlock({ &actual, 16, 512 });
        REQUIRE(actual.getSample(0, 116) == Catch::Approx(0.05f).margin(0.00001f)); // Panic, not audition.
        if (route == 0)
        {
            // An overflow may have dropped pedal-up. Panic must clear the old sustain state.
            output.collector.push(juce::MidiMessage::noteOn(1, 60, 0.5f));
            output.getNextAudioBlock({ &actual, 16, 512 });
            output.collector.push(juce::MidiMessage::noteOff(1, 60));
            output.getNextAudioBlock({ &actual, 16, 512 });
            output.getNextAudioBlock({ &actual, 16, 512 });
            REQUIRE(sampler.activeVoices() == 0);
        }
    }
}

TEST_CASE("Dense MIDI preview stays within the output safety knee", "[audio][midi-gain]")
{
    TestDirectory t;
    MidiSampler sampler; REQUIRE(sampler.load(t.wave()).isEmpty());
    Sf2KitSampler kit; Rex2SlicePlayer rex; SampleAudition audition; ExternalKitSampler external;
    juce::AudioTransportSource transport;
    PreviewOutput output(transport, sampler, kit, rex, audition, external);
    output.prepareToPlay(128, 44100); output.midi = true; output.gain = 1.0f;
    for (int i = 0; i < 16; ++i) output.collector.push(juce::MidiMessage::noteOn(1, 60, 1.0f));
    juce::AudioBuffer<float> buffer(2, 1024);
    output.getNextAudioBlock({ &buffer, 0, 1024 });
    REQUIRE(buffer.getMagnitude(0, 1024) > 0.9f);
    REQUIRE(buffer.getMagnitude(0, 1024) <= 1.0f);
}

TEST_CASE("Enabling MIDI preview does not boost ordinary transport", "[audio][midi-gain]")
{
    TestDirectory t;
    const auto file = t.wave();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    juce::AudioFormatReaderSource reader(formats.createReaderFor(file), true);
    juce::AudioFormatReaderSource referenceReader(formats.createReaderFor(file), true);
    juce::AudioTransportSource transport, reference;
    transport.setSource(&reader, 0, nullptr, 44100);
    reference.setSource(&referenceReader, 0, nullptr, 44100);
    MidiSampler sampler; Sf2KitSampler kit; Rex2SlicePlayer rex; SampleAudition audition; ExternalKitSampler external;
    PreviewOutput output(transport, sampler, kit, rex, audition, external);
    output.prepareToPlay(128, 44100); output.midi = true; output.gain = 0.5f;
    reference.prepareToPlay(128, 44100);
    transport.setGain(0.5f); reference.setGain(0.5f);
    transport.start(); reference.start();
    juce::AudioBuffer<float> actual(2, 128), expected(2, 128);
    output.getNextAudioBlock({ &actual, 0, 128 });
    reference.getNextAudioBlock({ &expected, 0, 128 });
    for (int i : { 50, 100, 127 })
        REQUIRE(actual.getSample(0, i) == Catch::Approx(expected.getSample(0, i)).margin(0.00001f));
    output.releaseResources(); reference.releaseResources();
    transport.setSource(nullptr); reference.setSource(nullptr);
}
