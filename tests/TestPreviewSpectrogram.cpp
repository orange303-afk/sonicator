#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/preview/PreviewPanel.h"
#include "ui/waveform/WaveformDisplay.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
SampleAudioPtr tone(int64_t frames = 44100, int sampleRate = 44100)
{
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = sampleRate;
    audio->channels = 1;
    audio->frames = frames;
    audio->left.resize((size_t) frames);
    for (int64_t i = 0; i < frames; ++i)
        audio->left[(size_t) i] = (float) (0.8 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                           * 440.0 * (double) i / (double) sampleRate));
    return audio;
}
} // namespace

TEST_CASE("Spectrum mode draws an image and keeps the selection", "[ui][spectrogram]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(480, 160);
    display.setEditedAudio(tone());
    display.setSelection({ 0.2, 0.4 });
    REQUIRE(display.spectrumAvailable());

    display.setRenderMode(WaveformDisplay::RenderMode::spectrum);
    REQUIRE(display.renderMode() == WaveformDisplay::RenderMode::spectrum);
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 8000));
    REQUIRE(display.spectrumRequestKeyForTest().isNotEmpty());
    REQUIRE(display.spectrumRequestKeyForTest() == display.spectrumReadyKeyForTest());
    // The time range belongs to the card, not the renderer, so it survives the mode switch.
    REQUIRE(display.selection().start == Approx(0.2).margin(1e-6));
    REQUIRE(display.selection().end == Approx(0.4).margin(1e-6));

    display.setSpectrumLogScale(false);
    REQUIRE_FALSE(display.spectrumLogScale());
    REQUIRE(pumpUntil([&]
    {
        const auto state = display.spectrumReadyKeyForTest();
        return state.isNotEmpty() && state == display.spectrumRequestKeyForTest();
    }, 8000));
}

TEST_CASE("A card without a source has no spectrum", "[ui][spectrogram]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(320, 120);
    REQUIRE_FALSE(display.spectrumAvailable());
    display.setSpectrogramAudio(tone());
    REQUIRE(display.spectrumAvailable());
    display.setSpectrogramAudio(nullptr);
    REQUIRE_FALSE(display.spectrumAvailable());
}

TEST_CASE("The preview shows the view tabs and remembers the mode", "[ui][spectrogram]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    preview.setSize(900, 220);
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    REQUIRE(preview.spectrumTabsVisible());
    REQUIRE_FALSE(preview.spectrumViewEnabled());

    preview.setSpectrumView(true, false);
    REQUIRE(preview.spectrumViewEnabled());
    // The ordinary-file path goes through the worker's own reader, not a decoded snapshot.
    REQUIRE(pumpUntil([&] { return preview.spectrumImageReady(); }, 8000));
    preview.setSpectrumLogScale(false, false);
    REQUIRE_FALSE(preview.spectrumLogScale());

    preview.setSpectrumView(false, false);
    REQUIRE_FALSE(preview.spectrumViewEnabled());
    REQUIRE(preview.spectrumTabsVisible());
}
