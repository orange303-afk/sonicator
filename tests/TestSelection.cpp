#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/waveform/WaveformDisplay.h"
#include "ui/waveform/WaveformSelection.h"
#include "ui/preview/PreviewPanel.h"
#include "core/audio/SelectionPlayback.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Waveform geometry maps pixels to time and back", "[selection][geometry]")
{
    REQUIRE(selection::timeAtX(6, 106, 10.0) == Catch::Approx(0.0));
    REQUIRE(selection::timeAtX(106, 106, 10.0) == Catch::Approx(10.0));
    REQUIRE(selection::timeAtX(-100, 106, 10.0) == Catch::Approx(0.0));
    REQUIRE(selection::timeAtX(1000, 106, 10.0) == Catch::Approx(10.0));
    REQUIRE(selection::xAtTime(5.0, 106, 10.0) == Catch::Approx(53.0).margin(0.01));
    REQUIRE(selection::timeAtX(selection::xAtTime(6.25, 106, 10.0), 106, 10.0)
            == Catch::Approx(6.25).margin(0.001));
}
TEST_CASE("Waveform selection orders and clamps its endpoints", "[selection][geometry]")
{
    auto r = selection::orderedRange(0.8, 0.2, 1.0);
    REQUIRE(r.start == Catch::Approx(0.2)); REQUIRE(r.end == Catch::Approx(0.8));
    REQUIRE(selection::orderedRange(-1.0, 0.4, 1.0).start == Catch::Approx(0.0));
    REQUIRE(selection::orderedRange(0.4, 9.0, 1.0).end == Catch::Approx(1.0));
    REQUIRE_FALSE(selection::orderedRange(0.5, 0.5, 1.0).valid());
    REQUIRE(selection::contains(r, 0.5)); REQUIRE_FALSE(selection::contains(r, 0.9));
}
TEST_CASE("Only drags beyond three pixels create a selection", "[selection][geometry]")
{
    REQUIRE_FALSE(selection::exceedsDragThreshold(3, 0));
    REQUIRE_FALSE(selection::exceedsDragThreshold(0, -3));
    REQUIRE(selection::exceedsDragThreshold(4, 0));
    REQUIRE(selection::exceedsDragThreshold(0, 4));
}
TEST_CASE("Selection range label uses seconds with millisecond precision", "[selection][label]")
{
    REQUIRE(selection::formatRange({0.25, 1.1}) == juce::String::fromUTF8("0.250–1.100 s"));
}
TEST_CASE("Setting a file or clearing notifies once and resets the selection", "[selection][state]")
{
    TestDirectory t; WaveformDisplay waveform(t.dir.getChildFile("cache"));
    int changes = 0; selection::Range seen;
    waveform.onSelectionChanged = [&](selection::Range r) { ++changes; seen = r; };
    waveform.setSelection({0.4, 0.2});
    REQUIRE(waveform.selection().start == Catch::Approx(0.2));
    REQUIRE(waveform.selection().end == Catch::Approx(0.4));
    REQUIRE(changes == 1);
    waveform.clearSelection();
    REQUIRE_FALSE(waveform.selection().valid());
    REQUIRE(changes == 2);
    waveform.clearSelection();
    REQUIRE(changes == 2);
    waveform.setSelection({0.1, 0.2});
    waveform.setFile(t.wave());
    REQUIRE_FALSE(waveform.selection().valid());
    REQUIRE(changes == 4);
    REQUIRE_FALSE(seen.valid());
}
TEST_CASE("Loading a new file clears the selection and hides Clear", "[selection][preview]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine,t.dir.getChildFile("cache"),t.dir.getChildFile("exports"));
    auto first = t.wave("first.wav"), second = t.wave("second.wav");
    REQUIRE(preview.loadFile(first));
    preview.selectRange(0.2, 0.5);
    REQUIRE(preview.selectedRange().valid());
    auto* clear = dynamic_cast<juce::TextButton*>(preview.findChildWithID("clearSelection"));
    auto* status = dynamic_cast<juce::Label*>(preview.findChildWithID("selectionStatus"));
    REQUIRE(clear != nullptr); REQUIRE(status != nullptr);
    REQUIRE(clear->isVisible());
    REQUIRE(preview.loadFile(second));
    REQUIRE_FALSE(preview.selectedRange().valid());
    REQUIRE_FALSE(clear->isVisible());
    REQUIRE(status->getText().isEmpty());
}
TEST_CASE("Selecting a range prepares a WAV and shows Ready", "[selection][preview]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine,t.dir.getChildFile("cache"),t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("prepare.wav",440,1,44100*2)));
    engine.stop();
    preview.selectRange(0.2, 0.5);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    const auto prepared = preview.preparedSelection();
    REQUIRE(prepared.existsAsFile());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(prepared));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 13230);
    auto* status = dynamic_cast<juce::Label*>(preview.findChildWithID("selectionStatus"));
    auto* clear = dynamic_cast<juce::TextButton*>(preview.findChildWithID("clearSelection"));
    REQUIRE(status != nullptr); REQUIRE(clear != nullptr);
    REQUIRE(status->getText().contains("Ready"));
    REQUIRE(clear->isVisible());
    preview.clearSelection();
    REQUIRE_FALSE(preview.selectedRange().valid());
    REQUIRE_FALSE(clear->isVisible());
    REQUIRE(status->getText().isEmpty());
}
TEST_CASE("Escape clears the selection and stops playback", "[selection][preview]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine,t.dir.getChildFile("cache"),t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("escape.wav")));
    engine.stop();
    preview.selectRange(0.2, 0.5);
    engine.play();
    REQUIRE(engine.isPlaying());
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    REQUIRE_FALSE(preview.selectedRange().valid());
    REQUIRE_FALSE(engine.isPlaying());
}
TEST_CASE("Clear does not take keyboard focus", "[selection][preview]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine,t.dir.getChildFile("cache"),t.dir.getChildFile("exports"));
    auto* clear = dynamic_cast<juce::TextButton*>(preview.findChildWithID("clearSelection"));
    REQUIRE(clear != nullptr);
    REQUIRE_FALSE(clear->getWantsKeyboardFocus());
    REQUIRE_FALSE(clear->getMouseClickGrabsKeyboardFocus());
}
TEST_CASE("Selection boundary stops or restarts at the range end", "[selection][audition]")
{
    using sonicator::SelectionBoundary;
    REQUIRE(selectionBoundary(0.5, 0.2, 0.8, false) == SelectionBoundary::none);
    REQUIRE(selectionBoundary(0.8, 0.2, 0.8, false) == SelectionBoundary::stop);
    REQUIRE(selectionBoundary(0.9, 0.2, 0.8, true) == SelectionBoundary::restart);
    REQUIRE(selectionBoundary(0.9, 0.8, 0.8, false) == SelectionBoundary::none);
}
TEST_CASE("Play with a selection starts at its beginning and stops at its end", "[selection][audition]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    auto file = t.wave("audition.wav", 440, 1, 44100 * 2);
    REQUIRE(preview.loadFile(file)); engine.stop();
    engine.transport().prepareToPlay(256, 44100);
    preview.selectRange(0.2, 0.35);
    engine.seek(1.5);
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    REQUIRE(engine.position() == Catch::Approx(0.2).margin(0.01));
    juce::AudioBuffer<float> buffer(2, 128);
    bool stopped = false;
    for (int i = 0; i < 400 && ! stopped; ++i)
    {
        engine.transport().getNextAudioBlock({&buffer, 0, 128});
        preview.updateSelectionPlayback();
        stopped = ! engine.isPlaying();
    }
    REQUIRE(stopped);
    REQUIRE(engine.position() >= 0.35);
    REQUIRE(engine.position() <= 0.35 + 128.0 / 44100.0 + 0.05);
    engine.transport().releaseResources();
}
TEST_CASE("Loop wraps selection playback back to its start", "[selection][audition]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("loop.wav", 440, 1, 44100 * 2))); engine.stop();
    engine.transport().prepareToPlay(256, 44100);
    preview.selectRange(0.2, 0.35);
    auto* loop = dynamic_cast<juce::ToggleButton*>(preview.findChildWithID("loop"));
    REQUIRE(loop != nullptr);
    loop->setToggleState(true, juce::sendNotificationSync);
    REQUIRE_FALSE(engine.isLooping());
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    juce::AudioBuffer<float> buffer(2, 128);
    bool reachedEnd = false, wrapped = false;
    for (int i = 0; i < 500 && ! wrapped; ++i)
    {
        engine.transport().getNextAudioBlock({&buffer, 0, 128});
        preview.updateSelectionPlayback();
        const auto p = engine.position();
        if (p >= 0.34) reachedEnd = true;
        if (reachedEnd && p < 0.25) wrapped = true;
    }
    REQUIRE(reachedEnd); REQUIRE(wrapped); REQUIRE(engine.isPlaying());
    engine.transport().releaseResources();
}
TEST_CASE("Preview paints a visible selection", "[selection][snapshot]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("snapshot.wav", 440, 1, 44100 * 2)));
    engine.stop();
    preview.setBounds(0, 0, 900, 156);
    preview.resized();
    const auto start = juce::Time::getMillisecondCounter();
    while (preview.waveformLength() <= 0 && juce::Time::getMillisecondCounter() - start < 3000)
        juce::Thread::sleep(5);
    REQUIRE(preview.waveformLength() > 0);
    preview.selectRange(0.2, 0.6);
    REQUIRE(preview.waitForSelectionPreparation(5000));
    const auto image = preview.createComponentSnapshot(preview.getLocalBounds());
    REQUIRE(image.isValid());
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_SELECTION_SCREENSHOT", {});
    if (path.isNotEmpty())
    {
        juce::File(path).deleteFile();
        auto output = juce::File(path).createOutputStream();
        REQUIRE(output != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(image, *output));
    }
}
TEST_CASE("A selection ending at EOF loops back without playing the file start", "[selection][audition]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("eof.wav", 440, 1, 44100))); engine.stop();
    engine.transport().prepareToPlay(256, 44100);
    preview.selectRange(0.8, 1.0);
    auto* loop = dynamic_cast<juce::ToggleButton*>(preview.findChildWithID("loop"));
    REQUIRE(loop != nullptr);
    loop->setToggleState(true, juce::sendNotificationSync);
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    juce::AudioBuffer<float> buffer(2, 128);
    double lowest = 1.0;
    bool wrapped = false;
    for (int i = 0; i < 600; ++i)
    {
        engine.transport().getNextAudioBlock({&buffer, 0, 128});
        preview.updateSelectionPlayback();
        const auto p = engine.position();
        lowest = juce::jmin(lowest, p);
        if (i > 10 && p < 0.85) wrapped = true;
    }
    REQUIRE(wrapped); REQUIRE(engine.isPlaying());
    REQUIRE(lowest >= 0.79);
    engine.transport().releaseResources();
}
