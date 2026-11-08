#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/audio/SampleAudition.h"
#include "core/export/AudioExporter.h"
#include "ui/preview/PreviewPanel.h"
#include "Sf2TestBank.h"
#include "TestHelpers.h"
#include <cmath>
using namespace sonicator;
using namespace sf2test;
namespace {
std::vector<juce::int16> ramp(int frames, int base)
{
    std::vector<juce::int16> pcm((size_t) frames);
    for (int i = 0; i < frames; ++i) pcm[(size_t) i] = (juce::int16) (base + i);
    return pcm;
}
juce::File writeTwoSampleBank(TestDirectory& t, int frames = 4410)
{
    auto pcm = ramp(frames * 2, 100);
    return writeBank(t, pcm,
        { { "Alpha", 0, frames, 0, 0, 44100 },
          { "Beta", frames, frames * 2, 0, 0, 44100 } },
        { { "Kit", { { 36, 36, 0, 127, 0 }, { 38, 38, 0, 127, 1 } } } },
        { { "Drums", 128, 0, { { 0 } } } });
}
std::unique_ptr<juce::AudioFormatReader> readerFor(const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}
}
TEST_CASE("Audition plays immutable PCM, pauses, seeks and preserves stereo", "[sf2][audition]")
{
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 2; audio.frames = 44100;
    audio.left.assign(44100, 0.5f);
    audio.right.assign(44100, -0.5f);
    auto held = std::make_shared<const SampleAudio>(audio);
    SampleAudition audition;
    audition.setAudio(held);
    audition.prepare(44100);
    REQUIRE(audition.hasAudio());
    REQUIRE(audition.length() == Catch::Approx(1.0).margin(0.001));
    REQUIRE_FALSE(audition.isPlaying());
    audition.play(false);
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear(); // Audition adds to its destination, like it does in PreviewOutput's mix.
    audition.render(buffer, 0, 512);
    REQUIRE(audition.isPlaying());
    REQUIRE(buffer.getSample(0, 100) == Catch::Approx(0.5f));
    REQUIRE(buffer.getSample(1, 100) == Catch::Approx(-0.5f));
    REQUIRE(audition.position() > 0.01);
    audition.pause();
    const auto paused = audition.position();
    audition.render(buffer, 0, 512);
    REQUIRE(audition.position() == Catch::Approx(paused));
    REQUIRE_FALSE(audition.isPlaying());
    audition.seek(0.5);
    audition.play(false);
    audition.render(buffer, 0, 128);
    REQUIRE(audition.position() > 0.5);
    REQUIRE(audition.position() < 0.55);
    audition.stop();
    REQUIRE(audition.position() == 0);
    REQUIRE_FALSE(audition.isPlaying());
    audition.setAudio({});
    REQUIRE_FALSE(audition.hasAudio());
}
TEST_CASE("Audition loops or stops at the end and resamples without changing pitch", "[sf2][audition]")
{
    SampleAudio audio;
    audio.sampleRate = 8000; audio.channels = 1; audio.frames = 800;
    audio.left.assign(800, 0.25f);
    auto held = std::make_shared<const SampleAudio>(audio);
    SampleAudition audition;
    audition.setAudio(held);
    audition.prepare(16000);
    REQUIRE(audition.length() == Catch::Approx(0.1).margin(0.0001));
    audition.play(false);
    juce::AudioBuffer<float> buffer(1, 64);
    for (int i = 0; i < 30; ++i) audition.render(buffer, 0, 64);
    REQUIRE_FALSE(audition.isPlaying());
    REQUIRE(audio.sampleRate == 8000);
    audition.seek(0.0);
    audition.play(true);
    for (int i = 0; i < 40; ++i) { buffer.clear(); audition.render(buffer, 0, 64); }
    REQUIRE(audition.isPlaying());
    REQUIRE(audition.position() < 0.1);
    REQUIRE(buffer.getMagnitude(0, 64) == Catch::Approx(0.25f));
}
TEST_CASE("AudioEngine exposes SF2 samples and auditions the selected source", "[sf2][audio]")
{
    TestDirectory t;
    auto bank = writeTwoSampleBank(t);
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(bank));
    REQUIRE(engine.isSoundFontMode());
    juce::String error;
    const auto samples = engine.soundFontSamples(error);
    REQUIRE(error.isEmpty());
    REQUIRE(samples.size() == 2);
    REQUIRE(samples[0].name == "Alpha");
    REQUIRE(samples[1].name == "Beta");
    REQUIRE(engine.soundFontSampleId() == 0);
    REQUIRE(engine.length() == Catch::Approx(4410.0 / 44100.0).margin(0.0001));
    REQUIRE(engine.selectSoundFontSample(1));
    REQUIRE(engine.soundFontSampleId() == 1);
    REQUIRE(engine.length() == Catch::Approx(4410.0 / 44100.0).margin(0.0001));
    auto audio = engine.soundFontSampleAudio();
    REQUIRE(audio != nullptr);
    REQUIRE(audio->left[0] == Catch::Approx((100.0 + 4410.0) / 32768.0).margin(0.0001));
    REQUIRE_FALSE(engine.isPlaying());
    juce::AudioBuffer<float> buffer(2, 256);
    engine.preparePreviewBlock(256);
    engine.play();
    REQUIRE(engine.isPlaying());
    engine.renderPreviewBlock(buffer, 256);
    REQUIRE(buffer.getMagnitude(0, 256) > 0.0f);
    engine.pause();
    REQUIRE_FALSE(engine.isPlaying());
    engine.seek(0.05);
    REQUIRE(engine.position() == Catch::Approx(0.05).margin(0.001));
    engine.stop();
    REQUIRE(engine.position() == 0);
    REQUIRE(engine.selectSoundFontPreset(0));
    REQUIRE(engine.soundFontSampleId() == 0);
    REQUIRE_FALSE(engine.selectSoundFontSample(99));
    REQUIRE(engine.error().isNotEmpty());
}
TEST_CASE("SF2 sample selection keeps the instrument MIDI mapping", "[sf2][audio][midi]")
{
    TestDirectory t;
    auto bank = writeTwoSampleBank(t);
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(bank));
    REQUIRE(engine.setMidiPreview(true));
    REQUIRE(engine.selectSoundFontSample(1));
    REQUIRE(engine.soundFontPresetIndex() == 0);
    juce::AudioBuffer<float> buffer(2, 256);
    engine.preparePreviewBlock(256);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    engine.renderPreviewBlock(buffer, 256);
    REQUIRE(engine.soundFontActiveVoices() == 1);
    REQUIRE(buffer.getMagnitude(0, 256) > 0.0f);
}
TEST_CASE("SF2 selection export writes the source PCM without gain or transpose", "[sf2][export]")
{
    TestDirectory t;
    auto pcm = ramp(44100, 100);
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1; audio.frames = 44100;
    audio.left.resize(44100);
    for (int i = 0; i < 44100; ++i) audio.left[(size_t) i] = (float) pcm[(size_t) i] / 32768.0f;
    const auto folder = t.dir.getChildFile("exports");
    const auto result = AudioExporter().exportSampleSelection(audio, "source", 0.2, 0.4, folder);
    REQUIRE(result.success);
    auto reader = readerFor(result.file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == 44100);
    REQUIRE(reader->numChannels == 1);
    REQUIRE(reader->bitsPerSample == 16);
    REQUIRE_FALSE(reader->usesFloatingPointData);
    REQUIRE(reader->lengthInSamples == 8820);
    juce::AudioBuffer<float> first(1, 2);
    REQUIRE(reader->read(&first, 0, 2, 0, true, true));
    REQUIRE(first.getSample(0, 0) == Catch::Approx(audio.left[8820]).margin(1.0 / 32768.0));
    REQUIRE(first.getSample(0, 1) == Catch::Approx(audio.left[8821]).margin(1.0 / 32768.0));
    const auto cancelled = AudioExporter().exportSampleSelection(audio, "source", 0.2, 0.4, folder,
        [] { return true; });
    REQUIRE_FALSE(cancelled.success);
    REQUIRE_FALSE(cancelled.file.existsAsFile());
}
TEST_CASE("SF2 preview lists samples, auditions silently and exports source PCM", "[sf2][ui][export]")
{
    TestDirectory t;
    const int frames = 44100;
    auto pcm = ramp(frames * 2, 100);
    auto bank = writeBank(t, pcm,
        { { "Alpha", 0, frames, 0, 0, 44100 },
          { "Beta", frames, frames * 2, 0, 0, 44100 } },
        { { "Kit", { { 36, 36, 0, 127, 0 }, { 38, 38, 0, 127, 1 } } } },
        { { "Drums", 128, 0, { { 0 } } } });
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(bank, false));
    REQUIRE_FALSE(engine.isPlaying());
    REQUIRE(preview.soundFontSampleCount() == 2);
    REQUIRE(preview.selectedSoundFontSampleId() == 0);
    REQUIRE(preview.waveformLength() == Catch::Approx(1.0).margin(0.001));
    auto* box = dynamic_cast<juce::ComboBox*>(preview.findChildWithID("instrumentSample"));
    REQUIRE(box != nullptr);
    REQUIRE_FALSE(box->isVisible());
    REQUIRE(box->getNumItems() == 2);
    auto& sources = preview.containerSourceList();
    REQUIRE(sources.isVisible());
    REQUIRE(sources.getListBoxModel()->getNumRows() == 3); // Head, Alpha, Beta
    REQUIRE(sources.getSelectedRow() == 0);
    sources.selectRow(2);
    sources.getListBoxModel()->selectedRowsChanged(2);
    REQUIRE(preview.selectedSoundFontSampleId() == 1);
    REQUIRE(engine.soundFontPresetIndex() == 0);
    REQUIRE(engine.isMidiPreview());
    REQUIRE(engine.isPlaying()); // the manual selection honours Auto Preview
    engine.stop();
    preview.selectRange(0.2, 0.4);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    auto reader = readerFor(preview.preparedSelection());
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == 44100);
    REQUIRE(reader->numChannels == 1);
    REQUIRE(reader->bitsPerSample == 16);
    REQUIRE(reader->lengthInSamples == 8820);
    juce::AudioBuffer<float> first(1, 1);
    REQUIRE(reader->read(&first, 0, 1, 0, true, true));
    REQUIRE(first.getSample(0, 0) == Catch::Approx((float) pcm[(size_t) (frames + 8820)] / 32768.0f)
        .margin(1.0 / 32768.0));
}
TEST_CASE("SF2 telemetry reports the zone that actually started, with velocity", "[sf2][telemetry]")
{
    TestDirectory t;
    const int frames = 1000;
    auto pcm = ramp(frames * 3, 100);
    auto bank = writeBank(t, pcm,
        { { "soft", 0, frames, 0, 0, 44100 },
          { "hard", frames, frames * 2, 0, 0, 44100 },
          { "other", frames * 2, frames * 3, 0, 0, 44100 } },
        { { "Kit", { { 36, 36, 0, 63, 0 }, { 36, 36, 64, 127, 1 }, { 38, 38, 0, 127, 2 } } } },
        { { "Drums", 128, 0, { { 0 } } } });
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(bank));
    REQUIRE(engine.setMidiPreview(true));
    REQUIRE_FALSE(engine.lastSoundFontTrigger().valid);
    juce::AudioBuffer<float> buffer(2, 256);
    engine.preparePreviewBlock(256);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 0.25f));
    engine.renderPreviewBlock(buffer, 256);
    auto trigger = engine.lastSoundFontTrigger();
    REQUIRE(trigger.valid);
    REQUIRE(trigger.sourceId == 0);
    REQUIRE(trigger.generation == engine.soundFontGeneration());
    REQUIRE(trigger.layers == 1);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    engine.renderPreviewBlock(buffer, 256);
    trigger = engine.lastSoundFontTrigger();
    REQUIRE(trigger.sourceId == 1);
    const auto before = trigger.sequence;
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 60, 1.0f)); // no matching zone
    engine.renderPreviewBlock(buffer, 256);
    trigger = engine.lastSoundFontTrigger();
    REQUIRE(trigger.sourceId == 1);
    REQUIRE(trigger.sequence == before);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 38, 1.0f));
    engine.renderPreviewBlock(buffer, 256);
    REQUIRE(engine.lastSoundFontTrigger().sourceId == 2);
}
TEST_CASE("SF2 telemetry ignores voice-limited notes and reports multi-layer sources", "[sf2][telemetry]")
{
    TestDirectory t;
    const int frames = 400;
    auto pcm = ramp(frames * 3, 100);
    auto bank = writeBank(t, pcm,
        { { "a", 0, frames, 0, 0, 44100 },
          { "b", frames, frames * 2, 0, 0, 44100 },
          { "stack", frames * 2, frames * 3, 0, 0, 44100 } },
        { { "Kit", { { 40, 40, 0, 127, 0 }, { 40, 40, 0, 127, 2 },
                     { 36, 36, 0, 127, 1 }, { 62, 62, 0, 127, 0 } } } },
        { { "Drums", 128, 0, { { 0 } } } });
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(bank));
    REQUIRE(engine.setMidiPreview(true));
    juce::AudioBuffer<float> buffer(2, 64);
    engine.preparePreviewBlock(64);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 40, 1.0f));
    engine.renderPreviewBlock(buffer, 64);
    auto trigger = engine.lastSoundFontTrigger();
    REQUIRE(trigger.sourceId == 0); // first zone in deterministic order
    REQUIRE(trigger.layers == 2);
    REQUIRE(engine.soundFontActiveVoices() == 2);
    for (int i = 0; i < Sf2KitSampler::maxVoices - 2; ++i)
        engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    engine.renderPreviewBlock(buffer, 64);
    REQUIRE(engine.soundFontActiveVoices() == Sf2KitSampler::maxVoices);
    const auto last = engine.lastSoundFontTrigger();
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 62, 1.0f)); // a zone but no free voice
    engine.renderPreviewBlock(buffer, 64);
    REQUIRE(engine.lastSoundFontTrigger().sequence == last.sequence);
    REQUIRE(engine.lastSoundFontTrigger().sourceId == last.sourceId);
}
TEST_CASE("SF2 MIDI follow updates the waveform but never while a selection is pinned", "[sf2][ui][telemetry]")
{
    TestDirectory t;
    const int alphaFrames = 4410, betaFrames = 8820;
    std::vector<juce::int16> pcm((size_t) (alphaFrames + betaFrames));
    for (int i = 0; i < (int) pcm.size(); ++i) pcm[(size_t) i] = (juce::int16) (100 + i % 1000);
    auto bank = writeBank(t, pcm,
        { { "Alpha", 0, alphaFrames, 0, 0, 44100 },
          { "Beta", alphaFrames, alphaFrames + betaFrames, 0, 0, 44100 } },
        { { "Kit", { { 36, 36, 0, 127, 0 }, { 38, 38, 0, 127, 1 } } } },
        { { "Drums", 128, 0, { { 0 } } } });
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(bank, false));
    REQUIRE(preview.selectedSoundFontSampleId() == 0);
    REQUIRE(preview.waveformLength() == Catch::Approx(0.1).margin(0.001));
    REQUIRE(engine.setMidiPreview(true));
    juce::AudioBuffer<float> buffer(2, 128);
    engine.preparePreviewBlock(128);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 38, 1.0f));
    engine.renderPreviewBlock(buffer, 128);
    preview.refreshMidiFollow();
    REQUIRE(preview.selectedSoundFontSampleId() == 1);
    REQUIRE(preview.waveformLength() == Catch::Approx(0.2).margin(0.001));
    REQUIRE_FALSE(engine.isPlaying()); // MIDI-follow must not start a duplicate audition
    preview.selectRange(0.05, 0.1);
    // MIDI keeps rendering while the worker prepares the pinned selection.
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    engine.renderPreviewBlock(buffer, 128);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    const auto pinnedId = preview.selectedSoundFontSampleId();
    const auto pinnedRange = preview.selectedRange();
    preview.refreshMidiFollow();
    REQUIRE(preview.selectedSoundFontSampleId() == pinnedId);
    REQUIRE(preview.selectedRange().start == pinnedRange.start);
    REQUIRE(preview.selectedRange().end == pinnedRange.end);
    REQUIRE(preview.isSelectionReady());
    REQUIRE(engine.soundFontActiveVoices() == 2); // instrument audio keeps sounding
    preview.clearSelection();
    preview.refreshMidiFollow();
    REQUIRE(preview.selectedSoundFontSampleId() == 0);
    REQUIRE(engine.soundFontActiveVoices() == 2);
}
TEST_CASE("SF2 Escape clears the pin without stopping MIDI, and manual change clears the range", "[sf2][ui][telemetry]")
{
    TestDirectory t;
    const int frames = 4410;
    auto pcm = ramp(frames * 2, 100);
    auto bank = writeBank(t, pcm,
        { { "Alpha", 0, frames, 0, 0, 44100 },
          { "Beta", frames, frames * 2, 0, 0, 44100 } },
        { { "Kit", { { 36, 36, 0, 127, 0 }, { 38, 38, 0, 127, 1 } } } },
        { { "Drums", 128, 0, { { 0 } } } });
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(bank, false));
    REQUIRE(engine.setMidiPreview(true));
    juce::AudioBuffer<float> buffer(2, 128);
    engine.preparePreviewBlock(128);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 38, 1.0f));
    engine.renderPreviewBlock(buffer, 128);
    preview.refreshMidiFollow();
    REQUIRE(preview.selectedSoundFontSampleId() == 1);
    preview.selectRange(0.02, 0.06);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    REQUIRE_FALSE(preview.selectedRange().valid());
    REQUIRE(engine.soundFontActiveVoices() == 1);
    preview.refreshMidiFollow();
    REQUIRE(preview.selectedSoundFontSampleId() == 1);
    auto* box = dynamic_cast<juce::ComboBox*>(preview.findChildWithID("instrumentSample"));
    REQUIRE(box != nullptr);
    preview.selectRange(0.02, 0.06);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    box->setSelectedId(1, juce::sendNotificationSync);
    REQUIRE(preview.selectedSoundFontSampleId() == 0);
    REQUIRE_FALSE(preview.selectedRange().valid());
    REQUIRE_FALSE(preview.isSelectionReady());
}
TEST_CASE("A new SF2 instrument generation discards the previous trigger", "[sf2][telemetry]")
{
    TestDirectory t;
    const int frames = 1000;
    auto pcm = ramp(frames * 2, 100);
    auto bank = writeBank(t, pcm,
        { { "first", 0, frames, 0, 0, 44100 },
          { "second", frames, frames * 2, 0, 0, 44100 } },
        { { "A", { { 36, 36, 0, 127, 0 } } }, { "B", { { 38, 38, 0, 127, 1 } } } },
        { { "One", 0, 0, { { 0 } } }, { "Two", 0, 1, { { 1 } } } });
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(bank));
    REQUIRE(engine.setMidiPreview(true));
    juce::AudioBuffer<float> buffer(2, 64);
    engine.preparePreviewBlock(64);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 36, 1.0f));
    engine.renderPreviewBlock(buffer, 64);
    const auto old = engine.lastSoundFontTrigger();
    REQUIRE(old.valid);
    REQUIRE(old.sourceId == 0);
    const auto oldGeneration = engine.soundFontGeneration();
    REQUIRE(engine.selectSoundFontPreset(1));
    REQUIRE(engine.soundFontGeneration() > oldGeneration);
    REQUIRE_FALSE(engine.lastSoundFontTrigger().valid);
    REQUIRE(engine.soundFontSampleId() == 1);
}
TEST_CASE("SF2 stereo selection export preserves both channels", "[sf2][export]")
{
    TestDirectory t;
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 2; audio.frames = 4410;
    audio.left.assign(4410, 0.5f);
    audio.right.assign(4410, -0.25f);
    const auto result = AudioExporter().exportSampleSelection(audio, "stereo", 0.0, 0.05,
                                                              t.dir.getChildFile("exports"));
    REQUIRE(result.success);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(result.file));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->numChannels == 2);
    REQUIRE(reader->lengthInSamples == 2205);
    juce::AudioBuffer<float> frame(2, 1);
    REQUIRE(reader->read(&frame, 0, 1, 0, true, true));
    REQUIRE(frame.getSample(0, 0) == Catch::Approx(0.5f).margin(1.0 / 32768.0));
    REQUIRE(frame.getSample(1, 0) == Catch::Approx(-0.25f).margin(1.0 / 32768.0));
}

TEST_CASE("SF2 MIDI follow preserves voices through the device player", "[.device][sf2][midi-tails]")
{
    TestDirectory t;
    const auto bank = writeTwoSampleBank(t, 44100);
    AudioEngine engine(true);
    REQUIRE(engine.deviceManager().getCurrentAudioDevice() != nullptr);
    const juce::ScopedLock callbackGuard(engine.deviceManager().getAudioCallbackLock());
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(bank, false));
    REQUIRE(engine.setMidiPreview(true));
    engine.preparePreviewBlock(256);
    juce::AudioBuffer<float> buffer(2, 256);
    for (int note : { 36, 38, 36 })
    {
        engine.midiCollector().push(juce::MidiMessage::noteOn(1, note, 1.0f));
        engine.renderPreviewBlock(buffer, 256);
        const auto voices = engine.instrumentActiveVoices();
        preview.refreshMidiFollow();
        REQUIRE(engine.instrumentActiveVoices() == voices);
    }
    REQUIRE(engine.instrumentActiveVoices() == 3);
    REQUIRE_FALSE(engine.isPlaying());
}

TEST_CASE("SF2 voices have two second release and 128 voice polyphony", "[sf2][midi-tails]")
{
    TestDirectory t;
    std::vector<juce::int16> pcm(4000, 16384);
    const auto file = writeBank(t, pcm, { { "tone", 0, 4000, 0, 0, 1000 } },
        { { "Instrument", { { 60, 60, 0, 127, 0 } } } }, { { "Preset", 0, 0, { { 0 } } } });
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(file, error));
    Sf2KitSampler sampler;
    REQUIRE(sampler.load(bank, 0, error));
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
    sampler.reset();
    for (int i = 0; i < 128; ++i) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 10, midi);
    REQUIRE(sampler.activeVoices() == 128);
    const auto sequence = sampler.lastTrigger().sequence;
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 10, midi);
    REQUIRE(sampler.lastTrigger().sequence == sequence);
    midi.clear(); midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    sampler.render(buffer, 0, 10, midi);
    REQUIRE(sampler.activeVoices() == 0);
    REQUIRE(buffer.getMagnitude(0, 10) == 0.0f);
}

TEST_CASE("Real SF2 exclusive groups keep their short choke release", "[sf2][midi-tails]")
{
    TestDirectory t;
    const auto file = writeBank(t, std::vector<juce::int16>(4000, 16384),
        { { "tone", 0, 4000, 0, 0, 1000 } },
        { { "Instrument", { { 60, 60, 0, 127, 0, 0, -1, 1 }, { 62, 62, 0, 127, 0, 0, -1, 1 } } } },
        { { "Preset", 0, 0, { { 0 } } } });
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(file, error));
    Sf2KitSampler sampler; REQUIRE(sampler.load(bank, 0, error));
    sampler.prepare(1000);
    juce::AudioBuffer<float> buffer(1, 100);
    juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    sampler.render(buffer, 0, 10, midi);
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    sampler.render(buffer, 0, 10, midi);
    REQUIRE(sampler.activeVoices() == 1);
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 0);
    sampler.render(buffer, 0, 100, midi);
    REQUIRE(sampler.activeVoices() == 1);
}
