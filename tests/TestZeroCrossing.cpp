#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/ZeroCrossing.h"
#include "core/settings/AppSettings.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/waveform/WaveformDisplay.h"
#include "ui/waveform/WaveformSelection.h"
#include "TestHelpers.h"
#include <cmath>
#include <memory>

using namespace sonicator;
using Catch::Approx;

namespace
{
// A square wave whose sign flips every `halfCycle` frames: the crossings sit exactly on multiples
// of `halfCycle`, which is what a snap has to find.
juce::AudioBuffer<float> squareWave(int frames, int halfCycle, float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buffer(1, frames);
    for (int i = 0; i < frames; ++i)
        buffer.setSample(0, i, ((i / halfCycle) % 2 == 0) ? amplitude : -amplitude);
    return buffer;
}

SampleAudioPtr squareSnapshot(int frames, int halfCycle, int rate)
{
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = rate;
    audio->channels = 1;
    audio->bitsPerSample = 16;
    audio->frames = frames;
    for (int i = 0; i < frames; ++i)
        audio->left.push_back(((i / halfCycle) % 2 == 0) ? 0.5f : -0.5f);
    return audio;
}

juce::MouseEvent waveformEvent(WaveformDisplay& display, juce::Point<float> position,
                               juce::Point<float> downPosition, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            1.0f, 0, 0, 0, 0, &display, &display, now, downPosition, now, 1,
                            dragged);
}

void dragSelection(WaveformDisplay& display, double fromSeconds, double toSeconds, double total)
{
    const auto from = juce::Point<float>(
        selection::xAtTime(fromSeconds, display.getWidth(), total), 60.0f);
    const auto to = juce::Point<float>(selection::xAtTime(toSeconds, display.getWidth(), total),
                                       60.0f);
    display.mouseDown(waveformEvent(display, from, from, false));
    display.mouseDrag(waveformEvent(display, to, from, true));
    display.mouseUp(waveformEvent(display, to, from, true));
}
} // namespace

TEST_CASE("A zero-crossing snap picks the nearest crossing", "[zerocross]")
{
    const auto buffer = squareWave(64, 16);
    CHECK(zeroCrossing::snapFrame(buffer, 10, 16) == 16);
    CHECK(zeroCrossing::snapFrame(buffer, 33, 16) == 32);
    CHECK(zeroCrossing::snapFrame(buffer, 31, 16) == 32);
    // Two crossings at the same distance: the earlier one wins, and the result is deterministic.
    CHECK(zeroCrossing::snapFrame(buffer, 40, 16) == 32);
    CHECK(zeroCrossing::snapFrame(buffer, 50, 8) == 48);
}

TEST_CASE("Without a crossing the snap takes the quietest frame, not a step", "[zerocross]")
{
    juce::AudioBuffer<float> dc(1, 32);
    for (int i = 0; i < 32; ++i) dc.setSample(0, i, 0.3f);
    // Every frame is equally loud, so the requested one stays.
    CHECK(zeroCrossing::snapFrame(dc, 12, 8) == 12);

    // A signal that only nearly reaches zero: the quietest sample of the window is the best edge.
    for (int i = 0; i < 32; ++i)
        dc.setSample(0, i, 0.4f + 0.001f * (float) std::abs(i - 20));
    CHECK(zeroCrossing::snapFrame(dc, 12, 8) == 20);
}

TEST_CASE("An empty or silent window leaves the frame where it was", "[zerocross]")
{
    juce::AudioBuffer<float> silent(1, 32);
    silent.clear();
    CHECK(zeroCrossing::snapFrame(silent, 12, 8) == 12);
    juce::AudioBuffer<float> empty(1, 0);
    CHECK(zeroCrossing::snapFrame(empty, 5, 8) == 5);
    // The radius never walks outside the buffer.
    const auto buffer = squareWave(20, 5);
    CHECK(zeroCrossing::snapFrame(buffer, 19, 1000) == 15);
}

TEST_CASE("A dragged range edge snaps to a zero crossing when Snap is on", "[zerocross][selection]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    const int rate = 8000, frames = 800; // 0.1 s, crossings every 20 frames = 2.5 ms
    display.setPeaks(std::vector<float>((size_t) frames, 0.2f), rate, {});
    display.setEditedAudio(squareSnapshot(frames, 20, rate));
    const double total = (double) frames / rate;
    REQUIRE(display.totalLength() == Approx(total));

    display.setSnapToZeroCrossing(true);
    dragSelection(display, 0.0031, 0.0072, total);
    REQUIRE(display.selection().valid());
    CHECK(display.selection().start == Approx(20.0 / rate).margin(1.0e-9));
    CHECK(display.selection().end == Approx(60.0 / rate).margin(1.0e-9));

    // The magnet is local: a drag started outside the range finds the crossing nearest to it.
    display.clearSelection();
    dragSelection(display, 0.0049, 0.0091, total);
    CHECK(display.selection().start == Approx(40.0 / rate).margin(1.0e-9));
    CHECK(display.selection().end == Approx(80.0 / rate).margin(1.0e-9));

    // A programmatic range is exact: snapping belongs to gestures, not to stored state.
    display.setSelection({ 0.00249, 0.00749 });
    CHECK(display.selection().start == 0.00249);
    CHECK(display.selection().end == 0.00749);
}

TEST_CASE("With Snap off the dragged edge keeps the pixel position", "[zerocross][selection]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    const int rate = 8000, frames = 800;
    display.setPeaks(std::vector<float>((size_t) frames, 0.2f), rate, {});
    display.setEditedAudio(squareSnapshot(frames, 20, rate));
    const double total = (double) frames / rate;
    REQUIRE_FALSE(display.snapToZeroCrossing());

    dragSelection(display, 0.0031, 0.0072, total);
    REQUIRE(display.selection().valid());
    CHECK(display.selection().start == Approx(0.0031).margin(0.0002));
    CHECK(display.selection().end == Approx(0.0072).margin(0.0002));
}

TEST_CASE("The Snap chip drives the card and survives the settings round trip",
          "[zerocross][ui]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    CHECK_FALSE(preview.snapToZeroCrossing());
    preview.setSnapToZeroCrossing(true, false);
    CHECK(preview.snapToZeroCrossing());
    CHECK(preview.waveformForTest().snapToZeroCrossing());
    preview.toggleSnapToZeroCrossing();
    CHECK_FALSE(preview.snapToZeroCrossing());
    CHECK_FALSE(preview.waveformForTest().snapToZeroCrossing());

    AppSettings settings;
    CHECK_FALSE(settings.snapToZeroCrossing);
    settings.snapToZeroCrossing = true;
    SettingsStore store(t.dir.getChildFile("settings.json"));
    store.save(settings);
    CHECK(store.load().snapToZeroCrossing);
}
