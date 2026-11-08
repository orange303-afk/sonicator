#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <cmath>
#include <vector>
using namespace sonicator;

namespace
{
SampleAudioPtr makeAudio(int frames, int channels, double rate)
{
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = (int) rate;
    audio->channels = channels;
    audio->bitsPerSample = 16;
    audio->frames = frames;
    audio->left.resize((size_t) frames);
    if (channels == 2) audio->right.resize((size_t) frames);
    for (int i = 0; i < frames; ++i)
    {
        audio->left[(size_t) i] = (float) (0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / rate));
        if (channels == 2) audio->right[(size_t) i] = -audio->left[(size_t) i];
    }
    return audio;
}
} // namespace

TEST_CASE("A disc sample plays through the ordinary transport", "[disc-preview]")
{
    AudioEngine engine(false);
    auto audio = makeAudio(4410, 1, 44100.0);
    REQUIRE(engine.setDiscSample(audio, false, 0, 0, false));
    REQUIRE(engine.isDiscMode());
    REQUIRE(engine.discSampleAudio() == audio);
    REQUIRE(engine.length() == Catch::Approx(0.1).margin(0.001));
    engine.preparePreviewBlock(256);
    engine.play();
    juce::AudioBuffer<float> buffer(2, 256);
    float peak = 0;
    for (int block = 0; block < 8; ++block)
    {
        engine.renderPreviewBlock(buffer, 256);
        peak = juce::jmax(peak, buffer.getMagnitude(0, 256));
    }
    REQUIRE(peak > 0.01f);
}

TEST_CASE("Disc MIDI plays the selected source chromatically and keeps stale sources out", "[disc-preview]")
{
    AudioEngine engine(false);
    REQUIRE(engine.setMidiPreview(true));
    auto audio = makeAudio(4410, 1, 44100.0);
    REQUIRE(engine.setDiscSample(audio, false, 0, 0, false));
    engine.preparePreviewBlock(256);
    engine.midiCollector().push(juce::MidiMessage::noteOn(1, 60, 1.0f));
    juce::AudioBuffer<float> buffer(2, 256);
    for (int block = 0; block < 4; ++block) engine.renderPreviewBlock(buffer, 256);
    REQUIRE(engine.discActiveVoices() > 0);
    REQUIRE(buffer.getMagnitude(0, 256) > 0.001f);

    const auto generation = engine.discGeneration();
    REQUIRE(engine.setDiscSample(makeAudio(2205, 2, 44100.0), false, 0, 0, false));
    REQUIRE(engine.discGeneration() > generation);
    REQUIRE(engine.discSampleAudio()->frames == 2205);

    engine.clearDiscSample();
    REQUIRE_FALSE(engine.isDiscMode());
    REQUIRE(engine.discSampleAudio() == nullptr);
}

TEST_CASE("A disc source exports its exact frames as WAV from the preview", "[disc-preview]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    auto container = t.dir.getChildFile("library.iso");
    container.replaceWithText("container identity only");
    std::vector<float> values(10);
    for (int i = 0; i < 10; ++i) values[(size_t) i] = (float) (i - 5) / 8.0f;
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = 44100; audio->channels = 1; audio->bitsPerSample = 16;
    audio->frames = 10; audio->left = values;
    REQUIRE(preview.loadDiscSample(container, "KICK", audio, false, 0, 0, false, false));
    REQUIRE(preview.waveformLength() == Catch::Approx(10.0 / 44100.0).margin(1.0e-9));
    REQUIRE(engine.isDiscMode());

    preview.selectRange(0.0, preview.waveformLength());
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(preview.preparedSelection()));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 10);
    REQUIRE(reader->sampleRate == 44100);
    juce::AudioBuffer<float> decoded(1, 10);
    REQUIRE(reader->read(&decoded, 0, 10, 0, true, false));
    REQUIRE(decoded.getSample(0, 0) == Catch::Approx(values[0]).margin(0.001f));
    REQUIRE(decoded.getSample(0, 9) == Catch::Approx(values[9]).margin(0.001f));

    preview.clearDiscSource();
    REQUIRE_FALSE(engine.isDiscMode());
    REQUIRE_FALSE(preview.selectedRange().valid());
}
