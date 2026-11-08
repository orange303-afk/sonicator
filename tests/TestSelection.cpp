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

namespace
{
juce::MouseEvent waveformClick(WaveformDisplay& waveform, float x, int clicks,
                               juce::ModifierKeys mods = juce::ModifierKeys())
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, 30.0f },
                            mods, 1.0f, 0, 0, 0, 0, &waveform, &waveform, now,
                            { x, 30.0f }, now, clicks, false);
}
void singleWaveformClick(WaveformDisplay& waveform, float x)
{
    const auto event = waveformClick(waveform, x, 1);
    waveform.mouseDown(event);
    waveform.mouseUp(event);
}
} // namespace

TEST_CASE("Double-clicking the waveform selects the whole source", "[selection][waveform-double-click]")
{
    TestDirectory t; WaveformDisplay waveform(t.dir.getChildFile("cache"));
    waveform.setSize(400, 120);
    waveform.setPeaks(std::vector<float>(1000, 0.2f), 100, {});
    REQUIRE(waveform.totalLength() == Catch::Approx(10.0));
    int changes = 0, seeks = 0; selection::Range last;
    waveform.onSelectionChanged = [&](selection::Range r) { ++changes; last = r; };
    waveform.onSeek = [&](double) { ++seeks; };

    singleWaveformClick(waveform, 120.0f);
    REQUIRE(seeks == 1);
    REQUIRE_FALSE(waveform.selection().valid());
    changes = 0;

    // JUCE delivers the second mouseUp before mouseDoubleClick, and that mouseUp carries 2 clicks.
    const auto second = waveformClick(waveform, 120.0f, 2);
    waveform.mouseDown(second);
    waveform.mouseUp(second);
    waveform.mouseDoubleClick(second);
    REQUIRE(waveform.selection().start == Catch::Approx(0.0));
    REQUIRE(waveform.selection().end == Catch::Approx(10.0));
    REQUIRE(last.start == Catch::Approx(0.0));
    REQUIRE(last.end == Catch::Approx(10.0));
    REQUIRE(changes == 1);
    REQUIRE(seeks == 1); // the second mouseUp of a double-click must not seek
}

TEST_CASE("A double-click replaces a partial selection without a duplicate export", "[selection][waveform-double-click]")
{
    TestDirectory t; WaveformDisplay waveform(t.dir.getChildFile("cache"));
    waveform.setSize(400, 120);
    waveform.setPeaks(std::vector<float>(1000, 0.2f), 100, {});
    int changes = 0;
    waveform.onSelectionChanged = [&](selection::Range) { ++changes; };
    waveform.setSelection({ 2.0, 4.0 });
    REQUIRE(changes == 1);

    singleWaveformClick(waveform, 300.0f); // first click of the pair clears the old range
    REQUIRE_FALSE(waveform.selection().valid());
    changes = 0;

    const auto second = waveformClick(waveform, 300.0f, 2);
    waveform.mouseDown(second);
    waveform.mouseUp(second);
    waveform.mouseDoubleClick(second);
    REQUIRE(waveform.selection().start == Catch::Approx(0.0));
    REQUIRE(waveform.selection().end == Catch::Approx(10.0));
    REQUIRE(changes == 1);
}

TEST_CASE("Double-clicking an already full selection does not republish it", "[selection][waveform-double-click]")
{
    TestDirectory t; WaveformDisplay waveform(t.dir.getChildFile("cache"));
    waveform.setSize(400, 120);
    waveform.setPeaks(std::vector<float>(1000, 0.2f), 100, {});
    int changes = 0;
    waveform.onSelectionChanged = [&](selection::Range) { ++changes; };
    waveform.setSelection({ 0.0, 10.0 });
    changes = 0;

    const auto second = waveformClick(waveform, 300.0f, 2);
    waveform.mouseDown(second);
    waveform.mouseUp(second);
    waveform.mouseDoubleClick(second);
    REQUIRE(changes == 0);
    REQUIRE(waveform.selection().start == Catch::Approx(0.0));
    REQUIRE(waveform.selection().end == Catch::Approx(10.0));
}

TEST_CASE("Double-clicking an empty waveform does nothing", "[selection][waveform-double-click]")
{
    TestDirectory t; WaveformDisplay waveform(t.dir.getChildFile("cache"));
    waveform.setSize(400, 120);
    int changes = 0, seeks = 0;
    waveform.onSelectionChanged = [&](selection::Range) { ++changes; };
    waveform.onSeek = [&](double) { ++seeks; };
    const auto second = waveformClick(waveform, 120.0f, 2);
    waveform.mouseDown(second);
    waveform.mouseUp(second);
    waveform.mouseDoubleClick(second);
    REQUIRE_FALSE(waveform.selection().valid());
    REQUIRE(changes == 0);
    REQUIRE(seeks == 0);
}

TEST_CASE("Right-button double-click does not select the waveform", "[selection][waveform-double-click]")
{
    TestDirectory t; WaveformDisplay waveform(t.dir.getChildFile("cache"));
    waveform.setSize(400, 120);
    waveform.setPeaks(std::vector<float>(1000, 0.2f), 100, {});
    int changes = 0;
    waveform.onSelectionChanged = [&](selection::Range) { ++changes; };
    const auto second = waveformClick(waveform, 120.0f, 2, juce::ModifierKeys::rightButtonModifier);
    waveform.mouseDown(second);
    waveform.mouseUp(second);
    waveform.mouseDoubleClick(second);
    REQUIRE_FALSE(waveform.selection().valid());
    REQUIRE(changes == 0);
}

TEST_CASE("Double-click then export covers the last frame of the source", "[selection][waveform-double-click]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("ten-frames.wav", 440, 1, 10)));
    engine.stop();
    WaveformDisplay waveform(t.dir.getChildFile("cache"));
    waveform.setSize(400, 120);
    waveform.setPeaks(std::vector<float>(10, 0.2f), 10, {});
    // Mirrors PreviewPanel's own wiring: the selected range drives the export worker.
    waveform.onSelectionChanged = [&](selection::Range r) { preview.selectRange(r.start, r.end); };

    const auto second = waveformClick(waveform, 120.0f, 2);
    waveform.mouseDown(second);
    waveform.mouseUp(second);
    waveform.mouseDoubleClick(second);
    REQUIRE(preview.selectedRange().valid());
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(preview.preparedSelection()));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 10);
}

namespace
{
juce::File writeConstantWave(TestDirectory& t, const juce::String& name, float value, int frames)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    auto out = file.createOutputStream();
    const int bytes = frames * 2;
    out->write("RIFF", 4); out->writeInt(bytes + 36); out->write("WAVEfmt ", 8);
    out->writeInt(16); out->writeShort(1); out->writeShort(1);
    out->writeInt(44100); out->writeInt(44100 * 2); out->writeShort(2); out->writeShort(16);
    out->write("data", 4); out->writeInt(bytes);
    for (int i = 0; i < frames; ++i) out->writeShort((short) std::lround(value * 32767));
    return file;
}
}

TEST_CASE("Selecting either L or R loads and exports one stereo file", "[selection][stereo-pair]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    auto left = writeConstantWave(t, "PAIR-L.wav", 0.25f, 100);
    auto right = writeConstantWave(t, "PAIR-R.wav", -0.5f, 100);
    REQUIRE(preview.loadFile(left, false));
    REQUIRE(preview.discSampleAudio() != nullptr);
    REQUIRE(preview.discSampleAudio()->channels == 2);
    REQUIRE(preview.discSampleAudio()->frames == 100);
    REQUIRE(preview.discSampleAudio()->left[0] == Catch::Approx(0.25f).margin(0.0001f));
    REQUIRE(preview.discSampleAudio()->right[0] == Catch::Approx(-0.5f).margin(0.0001f));

    preview.selectRange(0.0, preview.waveformLength());
    REQUIRE(preview.waitForSelectionPreparation(5000));
    REQUIRE(preview.isSelectionReady());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(preview.preparedSelection()));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->numChannels == 2);
    REQUIRE(reader->lengthInSamples == 100);
    juce::AudioBuffer<float> decoded(2, 100);
    REQUIRE(reader->read(&decoded, 0, 100, 0, true, true));
    REQUIRE(decoded.getSample(0, 0) == Catch::Approx(0.25f).margin(0.001f));
    REQUIRE(decoded.getSample(1, 0) == Catch::Approx(-0.5f).margin(0.001f));

    // Selecting the right file resolves the same pair, keeping L on the left.
    REQUIRE(preview.loadFile(right, false));
    REQUIRE(preview.discSampleAudio()->left[0] == Catch::Approx(0.25f).margin(0.0001f));
    REQUIRE(preview.discSampleAudio()->right[0] == Catch::Approx(-0.5f).margin(0.0001f));
}
