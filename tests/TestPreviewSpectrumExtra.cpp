#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/spectral/SpectrogramCache.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/waveform/WaveformDisplay.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
// TestHelpers writes 44.1 kHz fixtures, so 22050 samples are half a second.
constexpr int fixtureRate = 44100;
constexpr double fixtureDuration = 0.5;
constexpr double fixtureNyquist = 22050.0;

struct CacheFolderScope
{
    explicit CacheFolderScope(const juce::File& folder) { setSpectrumCacheFolderForTest(folder); }
    ~CacheFolderScope() { setSpectrumCacheFolderForTest({}); }
};

juce::MouseEvent spectralMouseEvent(WaveformDisplay& display, juce::Point<float> position,
                                    juce::Point<float> downPosition, bool dragged,
                                    juce::ModifierKeys mods = juce::ModifierKeys(
                                        juce::ModifierKeys::leftButtonModifier))
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                            1.0f, 0, 0, 0, 0, &display, &display, now, downPosition, now, 1, dragged);
}

// Pixel position of a time and a frequency inside the card, using the mapping the card documents:
// 6 px inset on both sides, log frequency from 20 Hz to the source's Nyquist.
juce::Point<float> spectrumPoint(double seconds, double hz, int width, int height)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height)
                          .reduced(6.0f, 10.0f);
    const auto track = (double) width - 12.0;
    const auto position = std::log(hz / 20.0) / std::log(fixtureNyquist / 20.0);
    return { (float) (6.0 + seconds / fixtureDuration * track),
             area.getBottom() - (float) position * area.getHeight() };
}

int prepareDisplay(WaveformDisplay& display, const juce::File& file)
{
    display.setSize(480, 160);
    display.setFile(file);
    display.setRenderMode(WaveformDisplay::RenderMode::spectrum);
    return display.spectrumImageReadyForTest() ? 0 : 1;
}

SpectralMask bandMask()
{
    SpectralMask mask;
    mask.setWindow(0.2, 0.4, fixtureNyquist);
    mask.addRectangle(0.2, 0.4, 200.0, 1500.0, false);
    return mask;
}
} // namespace

TEST_CASE("A cached file opens on its stored overview immediately", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    const auto file = t.wave("sine.wav", 440.0, 1, 22050);
    {
        WaveformDisplay display(t.dir.getChildFile("waveforms"));
        prepareDisplay(display, file);
        REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));
        REQUIRE(spectrumCacheIsFresh(file));
    }
    // The second card reads the .sps in setFile, before any timer runs.
    WaveformDisplay second(t.dir.getChildFile("waveforms"));
    prepareDisplay(second, file);
    REQUIRE(second.spectrumImageReadyForTest());
}

TEST_CASE("The spectral overlay covers its band and nothing else", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    bool logScale = true;
    SECTION("Logarithmic axis") {}
    SECTION("Linear axis starting at zero Hz") { logScale = false; }
    display.setSpectrumLogScale(logScale);
    prepareDisplay(display, t.wave("sine.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));
    const auto before = display.createComponentSnapshot(display.getLocalBounds(), true);
    REQUIRE(before.isValid());

    display.setSpectralMaskForTest(bandMask());
    const auto after = display.createComponentSnapshot(display.getLocalBounds(), true);
    REQUIRE(after.isValid());

    // The fill stays inside the band; not even the time edges mark the rest of the card.
    const int bandCentreY = logScale ? 84 : 144;
    CHECK(before.getPixelAt(287, bandCentreY) != after.getPixelAt(287, bandCentreY));
    for (int x = 0; x < before.getWidth(); ++x)
        REQUIRE(before.getPixelAt(x, 30) == after.getPixelAt(x, 30));

    int left = before.getWidth(), right = -1;
    for (int y = 0; y < before.getHeight(); ++y)
        for (int x = 0; x < before.getWidth(); ++x)
            if (before.getPixelAt(x, y) != after.getPixelAt(x, y))
            {
                left = juce::jmin(left, x);
                right = juce::jmax(right, x);
            }
    REQUIRE(right > left);
    // 0.2 s and 0.4 s of a half-second file on a 480 px card: x = 193 and x = 380, not the
    // whole width the old stretched drawing produced.
    INFO("changed columns " << left << ".." << right);
    REQUIRE(left >= 189);
    REQUIRE(left <= 198);
    REQUIRE(right >= 375);
    REQUIRE(right <= 385);

    // Inside those edges the overlay must sit on its own band, not on the whole height.
    int top = before.getHeight(), bottom = -1;
    for (int y = 0; y < before.getHeight(); ++y)
        for (int x = juce::jmax(0, left + 3); x <= juce::jmin(before.getWidth() - 1, right - 3); ++x)
            if (before.getPixelAt(x, y) != after.getPixelAt(x, y))
            {
                top = juce::jmin(top, y);
                bottom = juce::jmax(bottom, y);
            }
    REQUIRE(bottom > top);
    INFO("changed rows " << top << ".." << bottom);
    REQUIRE(top >= (logScale ? 58 : 137));
    REQUIRE(bottom <= (logScale ? 112 : 149));
}

TEST_CASE("Spectral tools hide the Time overlay without discarding the range", "[spectral][ui]")
{
    using Tool = WaveformDisplay::SelectionTool;
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    prepareDisplay(display, t.wave("sine.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));

    const auto visiblePixel = [&]
    {
        return display.createComponentSnapshot(display.getLocalBounds(), true).getPixelAt(287, 80);
    };
    const auto unselected = visiblePixel();
    display.setSelection({ 0.2, 0.4 });
    const auto selected = visiblePixel();
    REQUIRE(selected != unselected);

    for (const auto tool : { Tool::rectangle, Tool::lasso, Tool::wand })
    {
        display.setSelectionTool(tool);
        CHECK(visiblePixel() == unselected);
        CHECK(display.selection().valid());
    }
    display.setSelectionTool(Tool::time);
    CHECK(visiblePixel() == selected);
}

TEST_CASE("A press on the selection lifts it as it always did", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    prepareDisplay(display, t.wave("sine.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));
    display.setSelectionTool(WaveformDisplay::SelectionTool::rectangle);

    const auto mask = bandMask();
    display.setSpectralMaskForTest(mask);
    int requests = 0;
    display.onExternalDragRequested = [&requests] { ++requests; };

    const auto inside = spectrumPoint(0.3, 500.0, 480, 160);
    display.mouseDown(spectralMouseEvent(display, inside, inside, false));

    // Leaving the card is the lift: the prepared masked WAV goes to the DAW and the mask stays.
    const juce::Point<float> farOutside(520.0f, 80.0f);
    display.mouseDrag(spectralMouseEvent(display, farOutside, inside, true));
    display.mouseUp(spectralMouseEvent(display, farOutside, inside, true));
    REQUIRE(requests == 1);
    REQUIRE(display.spectralMask() != nullptr);
    REQUIRE(display.spectralMask()->maskedCellCount() == mask.maskedCellCount());

    // A press on the selection lifts it: any drag from inside asks for the external file drag and
    // the mask stays as it is (the behaviour the card had when the tools worked).
    const auto insideEnd = spectrumPoint(0.42, 1200.0, 480, 160);
    display.mouseDown(spectralMouseEvent(display, inside, inside, false));
    display.mouseDrag(spectralMouseEvent(display, insideEnd, inside, true));
    display.mouseUp(spectralMouseEvent(display, insideEnd, inside, true));
    REQUIRE(requests == 2);
    REQUIRE(display.spectralMask() != nullptr);
    REQUIRE(display.spectralMask()->maskedCellCount() == mask.maskedCellCount());

    // A press outside the selection keeps drawing there, replacing it.
    const auto outside = spectrumPoint(0.45, 5000.0, 480, 160);
    const auto faraway = spectrumPoint(0.48, 3000.0, 480, 160);
    display.mouseDown(spectralMouseEvent(display, outside, outside, false));
    display.mouseDrag(spectralMouseEvent(display, faraway, outside, true));
    display.mouseUp(spectralMouseEvent(display, faraway, outside, true));
    REQUIRE(requests == 2);
    REQUIRE(display.spectralMask() != nullptr);
    REQUIRE(display.spectralMask()->startSeconds > 0.4);
    REQUIRE(display.spectralMask()->endSeconds < 0.5);

    // Shift edits the mask even on top of it, so the same press on the band draws instead.
    display.setSpectralMaskForTest(mask);
    const auto shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier
                                         | juce::ModifierKeys::shiftModifier);
    const auto cellsBefore = display.spectralMask()->maskedCellCount();
    display.mouseDown(spectralMouseEvent(display, inside, inside, false, shift));
    display.mouseDrag(spectralMouseEvent(display, outside, inside, true, shift));
    display.mouseUp(spectralMouseEvent(display, outside, inside, true, shift));
    REQUIRE(requests == 2);
    REQUIRE(display.spectralMask() != nullptr);
    REQUIRE(display.spectralMask()->maskedCellCount() > cellsBefore);
}

TEST_CASE("Each spectrum tool has its own cursor and the selection answers with a hand",
          "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    prepareDisplay(display, t.wave("sine.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));

    display.setSelectionTool(WaveformDisplay::SelectionTool::rectangle);
    REQUIRE(display.spectralCursorKindForTest() == 0);
    display.setSelectionTool(WaveformDisplay::SelectionTool::lasso);
    REQUIRE(display.spectralCursorKindForTest() == 1);
    display.setSelectionTool(WaveformDisplay::SelectionTool::wand);
    REQUIRE(display.spectralCursorKindForTest() == 2);
    display.setSelectionTool(WaveformDisplay::SelectionTool::time);
    REQUIRE(display.spectralCursorKindForTest() == -1);

    display.setSpectralMaskForTest(bandMask());
    display.setSelectionTool(WaveformDisplay::SelectionTool::rectangle);
    const auto inside = spectrumPoint(0.3, 500.0, 480, 160);
    const auto outside = spectrumPoint(0.45, 5000.0, 480, 160);
    display.mouseMove(spectralMouseEvent(display, inside, inside, false));
    REQUIRE(display.getMouseCursor() == juce::MouseCursor::DraggingHandCursor);
    display.mouseMove(spectralMouseEvent(display, outside, outside, false));
    REQUIRE_FALSE(display.getMouseCursor() == juce::MouseCursor::DraggingHandCursor);
    display.setSelectionTool(WaveformDisplay::SelectionTool::time);
    display.mouseMove(spectralMouseEvent(display, inside, inside, false));
    REQUIRE(display.getMouseCursor() == juce::MouseCursor::NormalCursor);
}

TEST_CASE("Lasso and wand fill the region under the pointer", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    prepareDisplay(display, t.wave("tone.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));

    // A four-corner lasso around 0.22..0.38 s and 300..1200 Hz.
    display.setSelectionTool(WaveformDisplay::SelectionTool::lasso);
    const juce::Point<float> corners[] = {
        spectrumPoint(0.22, 300.0, 480, 160), spectrumPoint(0.38, 300.0, 480, 160),
        spectrumPoint(0.38, 1200.0, 480, 160), spectrumPoint(0.22, 1200.0, 480, 160)
    };
    display.mouseDown(spectralMouseEvent(display, corners[0], corners[0], false));
    for (int i = 1; i < 4; ++i)
        display.mouseDrag(spectralMouseEvent(display, corners[i], corners[0], true));
    display.mouseUp(spectralMouseEvent(display, corners[3], corners[0], true));
    REQUIRE(display.spectralMask() != nullptr);
    INFO("lasso cells " << display.spectralMask()->maskedCellCount());
    REQUIRE(display.spectralMask()->contains(0.3, 600.0));
    REQUIRE_FALSE(display.spectralMask()->contains(0.3, 4000.0));
    REQUIRE_FALSE(display.spectralMask()->contains(0.05, 600.0));

    // The wand fills the ridge the tone leaves, and stops at the noise floor around it.
    display.setSelectionTool(WaveformDisplay::SelectionTool::wand);
    const auto onTone = spectrumPoint(0.25, 440.0, 480, 160);
    display.mouseDown(spectralMouseEvent(display, onTone, onTone, false));
    display.mouseUp(spectralMouseEvent(display, onTone, onTone, false));
    REQUIRE(display.spectralMask() != nullptr);
    INFO("wand cells " << display.spectralMask()->maskedCellCount()
                       << " band " << display.spectralMask()->lowFrequency() << ".."
                       << display.spectralMask()->highFrequency());
    REQUIRE(display.spectralMask()->contains(0.25, 440.0));
    REQUIRE(display.spectralMask()->contains(0.4, 440.0));
    REQUIRE_FALSE(display.spectralMask()->contains(0.25, 6000.0));
    REQUIRE(display.spectralMask()->highFrequency() < 1000.0);
}

TEST_CASE("A looping spectral audition keeps sounding past its first pass", "[spectral][audio]")
{
    TestDirectory t;
    // A prepared fragment: 0.2 s of tone, exactly what the masked export produces.
    const auto fragment = t.wave("fragment.wav", 440.0, 1, 8820);
    AudioEngine engine(false);
    engine.startHostAudio(44100, 256);
    REQUIRE(engine.setMatchedTransport(fragment));
    engine.setMatchedLooping(true);
    engine.play();

    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    int silent = 0;
    const int blocks = (int) (0.9 * 44100.0 / 256.0); // more than four passes of the fragment
    for (int i = 0; i < blocks; ++i)
    {
        buffer.clear();
        engine.processHostBlock(buffer, midi);
        if (buffer.getMagnitude(0, buffer.getNumSamples()) < 0.01f) ++silent;
    }
    // The read-ahead buffer would wrap on its own modulo the length and go silent after one pass.
    INFO("silent blocks " << silent << " of " << blocks);
    REQUIRE(silent == 0);
    engine.stop();
}

TEST_CASE("MIDI preview plays the masked fragment, not the whole source", "[spectral][audio]")
{
    TestDirectory t;
    const auto source = t.wave("source.wav", 440.0, 1, 44100);      // 1 s
    const auto fragment = t.wave("fragment.wav", 440.0, 1, 4410);   // 0.1 s
    AudioEngine engine(false);
    engine.startHostAudio(44100, 256);
    REQUIRE(engine.loadFile(source));
    REQUIRE(engine.setMidiPreview(true));

    // Head: the first 0.1 s after the key. Tail: 0.15..0.3 s, when a 0.1 s fragment is over.
    const auto peaks = [&engine](float& head, float& tail)
    {
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        const int blocks = (int) (0.3 * 44100.0 / 256.0);
        head = tail = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            engine.processHostBlock(buffer, midi);
            midi.clear();
            const auto peak = buffer.getMagnitude(0, buffer.getNumSamples());
            const int firstFrame = i * 256;
            if (firstFrame < (int) (0.1 * 44100.0)) head = juce::jmax(head, peak);
            if (firstFrame >= (int) (0.15 * 44100.0)) tail = juce::jmax(tail, peak);
        }
    };

    float sourceHead = 0.0f, sourceTail = 0.0f;
    peaks(sourceHead, sourceTail);
    REQUIRE(sourceHead > 0.01f);
    REQUIRE(sourceTail > 0.01f);
    engine.stop();
    engine.setMidiFragment(fragment);
    REQUIRE(engine.midiFragmentFile() == fragment);
    float fragmentHead = 0.0f, fragmentTail = 0.0f;
    peaks(fragmentHead, fragmentTail);
    INFO("peaks: source " << sourceHead << "/" << sourceTail << ", fragment " << fragmentHead
                          << "/" << fragmentTail);
    // The fragment is played (it sounds at once) and it is only the fragment: no tail of the
    // untouched one-second source.
    REQUIRE(fragmentHead > 0.01f);
    REQUIRE(fragmentTail < fragmentHead * 0.05f);
    engine.stop();

    // Clearing the fragment returns the source to the sampler instead of leaving stale PCM.
    engine.setMidiFragment({});
    REQUIRE(engine.midiFragmentFile() == juce::File());
    float restoredHead = 0.0f, restoredTail = 0.0f;
    peaks(restoredHead, restoredTail);
    INFO("restored peaks " << restoredHead << "/" << restoredTail);
    REQUIRE(restoredHead > 0.01f);
    REQUIRE(restoredTail > fragmentTail * 20.0f);
    engine.stop();
}

TEST_CASE("A prepared spectral mask becomes the MIDI fragment", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    preview.setSize(900, 220);
    REQUIRE(preview.loadFile(t.wave("sine.wav", 440.0, 1, 44100), false));
    preview.setSpectrumView(true, false);

    SpectralMask mask;
    mask.setWindow(0.1, 0.3, 22050.0);
    mask.addRectangle(0.1, 0.3, 200.0, 1500.0, false);
    preview.waveformForTest().setSpectralMaskForTest(mask);
    REQUIRE(pumpUntil([&] { return preview.spectralExportReady(); }, 20000));
    const auto exported = preview.selectionExportState().file;
    REQUIRE(exported.existsAsFile());
    // The panel's timer hands the prepared WAV to the engine; MIDI preview then plays it whole.
    REQUIRE(pumpUntil([&] { return engine.midiFragmentFile() == exported; }, 20000));

    preview.clearSpectralSelection();
    REQUIRE_FALSE(preview.spectralSelectionActive());
    REQUIRE(engine.midiFragmentFile() == juce::File());
}

TEST_CASE("A rectangle drag in the card's spectrum creates and draws a mask", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    preview.setSize(900, 260);
    REQUIRE(preview.loadFile(t.wave("sine.wav", 440.0, 1, 44100), false));
    preview.setSpectrumView(true, false);
    auto& display = preview.waveformForTest();
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));
    display.setSelectionTool(WaveformDisplay::SelectionTool::rectangle);
    REQUIRE(display.getWidth() > 200);

    const auto before = display.createComponentSnapshot(display.getLocalBounds(), true);
    const auto duration = (double) display.totalLength();
    REQUIRE(duration > 0.5);
    const juce::Point<float> from(
        (float) (selection::inset + 0.2 / duration * ((double) display.getWidth() - 2 * selection::inset)),
        (float) display.getHeight() * 0.35f);
    const juce::Point<float> to(
        (float) (selection::inset + 0.7 / duration * ((double) display.getWidth() - 2 * selection::inset)),
        (float) display.getHeight() * 0.7f);
    const auto press = spectralMouseEvent(display, from, from, false);
    display.mouseDown(press);
    display.mouseDrag(spectralMouseEvent(display, to, from, true));
    display.mouseUp(spectralMouseEvent(display, to, from, true));

    INFO("mask: " << (display.spectralMask() != nullptr));
    REQUIRE(display.spectralMask() != nullptr);
    CHECK(display.spectralMask()->maskedCellCount() > 0);

    const auto after = display.createComponentSnapshot(display.getLocalBounds(), true);
    int changed = 0;
    for (int y = 0; y < before.getHeight(); ++y)
        for (int x = 0; x < before.getWidth(); ++x)
            if (before.getPixelAt(x, y) != after.getPixelAt(x, y)) ++changed;
    INFO("changed pixels " << changed);
    CHECK(changed > 500);
}

TEST_CASE("A plain drag in the spectrum still makes a time selection", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    prepareDisplay(display, t.wave("sine.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));
    REQUIRE(display.selectionTool() == WaveformDisplay::SelectionTool::time);

    const auto from = spectrumPoint(0.1, 3000.0, 480, 160);
    const auto to = spectrumPoint(0.4, 3000.0, 480, 160);
    display.mouseDown(spectralMouseEvent(display, from, from, false));
    display.mouseDrag(spectralMouseEvent(display, to, from, true));
    display.mouseUp(spectralMouseEvent(display, to, from, true));
    INFO("selection " << display.selection().start << ".." << display.selection().end);
    REQUIRE(display.selection().valid());
    CHECK(display.selection().start == Approx(0.1).margin(0.01));
    CHECK(display.selection().end == Approx(0.4).margin(0.01));
    CHECK(display.spectralMask() == nullptr);
}

TEST_CASE("Clicking the Rect chip and dragging makes a mask", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    prepareDisplay(display, t.wave("sine.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));

    // The chip sits in the card's top-left strip: 34 px wide, a 3 px gap, 18 px tall, inset by 4.
    const auto chip = juce::Point<float>(6.0f + 4.0f + 34.0f + 3.0f + 17.0f, 10.0f + 4.0f + 9.0f);
    display.mouseDown(spectralMouseEvent(display, chip, chip, false));
    display.mouseUp(spectralMouseEvent(display, chip, chip, false));
    INFO("tool after the click: " << (int) display.selectionTool());
    REQUIRE(display.selectionTool() == WaveformDisplay::SelectionTool::rectangle);

    const auto from = spectrumPoint(0.2, 400.0, 480, 160);
    const auto to = spectrumPoint(0.4, 2000.0, 480, 160);
    display.mouseDown(spectralMouseEvent(display, from, from, false));
    display.mouseDrag(spectralMouseEvent(display, to, from, true));
    display.mouseUp(spectralMouseEvent(display, to, from, true));
    REQUIRE(display.spectralMask() != nullptr);
    CHECK(display.spectralMask()->maskedCellCount() > 0);
}

TEST_CASE("A wand drag fills and never starts a time selection", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    WaveformDisplay display(t.dir.getChildFile("waveforms"));
    prepareDisplay(display, t.wave("tone.wav", 440.0, 1, 22050));
    REQUIRE(pumpUntil([&] { return display.spectrumImageReadyForTest(); }, 20000));
    display.setSelectionTool(WaveformDisplay::SelectionTool::wand);

    const auto onTone = spectrumPoint(0.3, 440.0, 480, 160);
    const auto away = spectrumPoint(0.5, 3000.0, 480, 160);
    display.mouseDown(spectralMouseEvent(display, onTone, onTone, false));
    display.mouseDrag(spectralMouseEvent(display, away, onTone, true));
    display.mouseUp(spectralMouseEvent(display, away, onTone, true));
    REQUIRE(display.spectralMask() != nullptr);
    CHECK(display.spectralMask()->maskedCellCount() > 0);
    // The Time tool would have made a range; the wand must not.
    CHECK_FALSE(display.selection().valid());
}

TEST_CASE("A spectral mask prepares a masked export and auditions it", "[spectral][ui]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    preview.setSize(900, 220);
    REQUIRE(preview.loadFile(t.wave("sine.wav", 440.0, 1, 44100), false));
    preview.setSpectrumView(true, false);

    SpectralMask mask;
    mask.setWindow(0.1, 0.3, 22050.0);
    mask.addRectangle(0.1, 0.3, 200.0, 1500.0, false);
    preview.waveformForTest().setSpectralMaskForTest(mask);
    REQUIRE(preview.spectralSelectionActive());
    REQUIRE(pumpUntil([&] { return preview.spectralExportReady(); }, 20000));

    preview.playPause();
    REQUIRE(preview.spectralAuditionActive());
    preview.stopPlayback();
    REQUIRE_FALSE(preview.spectralAuditionActive());
}
