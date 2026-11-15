#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include "plugin/PluginState.h"
#include "ui/MainComponent.h"
#include "ui/preview/PreviewPanel.h"
#include "core/database/SampleRepository.h"
#include "core/audio/SonicatorFormats.h"
#include "TestHelpers.h"
#include <cmath>

using namespace sonicator;

namespace
{
juce::File writeLoop (const juce::File& file, double seconds = 8.0)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    REQUIRE (stream != nullptr);
    auto options = juce::AudioFormatWriterOptions {}
        .withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (16);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);
    REQUIRE (writer != nullptr);
    const int frames = (int) std::llround (seconds * 44100.0);
    juce::AudioBuffer<float> buffer (1, frames);
    for (int i = 0; i < frames; ++i)
        buffer.setSample (0, i, 0.4f * std::sin (juce::MathConstants<float>::twoPi * 440.0f
                                                 * (float) i / 44100.0f));
    writer->writeFromAudioSampleBuffer (buffer, 0, frames);
    writer->flush();
    return file;
}

void press (juce::Button& button)
{
    button.setToggleState (! button.getToggleState(), juce::dontSendNotification);
    if (button.onClick) button.onClick();
}

struct PreviewMatchHarness
{
    TestDirectory t;
    AudioEngine engine { false };
    PreviewPanel preview { engine, t.dir.getChildFile ("cache"), t.dir.getChildFile ("exports") };
    PluginMatchPreferences preferences;
    plugin::HostTempoValue tempo { true, 128.0, 1 };
    juce::File sourceFile;
    MatchSource source;

    PreviewMatchHarness()
    {
        sourceFile = writeLoop (t.dir.getChildFile ("loop.wav"));
        source.identity = "catalog:1:" + sourceFile.getFullPathName();
        source.sourceFrames = (int64_t) std::llround (8.0 * 44100.0);
        source.sampleRate = 44100.0;
        source.channels = 1;
        source.structure = MatchStructure::loop;
        source.bpm = 120.0;
        source.bpmState = MetadataState::known;
        source.keyTonic = 9; // A
        source.keyMode = "minor";
        source.keyState = MetadataState::known;
        source.metadataRevision = 3;

        PluginMatchContext context;
        context.getPreferences = [this] { return preferences; };
        context.setPreferences = [this] (const PluginMatchPreferences& next) { preferences = next; };
        context.hostTempo = [this] { return tempo; };
        context.sourceInfo = [this] (const juce::File& file, int64_t& size, int64_t& modified)
            -> std::optional<MatchSource>
        {
            if (file != sourceFile) return std::nullopt;
            size = sourceFile.getSize();
            modified = sourceFile.getLastModificationTime().toMilliseconds();
            return source;
        };
        preview.setPluginMatchContext (std::move (context));
        preview.setSize (620, 420);
        REQUIRE (preview.loadFile (sourceFile, false));
    }

    juce::ToggleButton& toggle (const juce::String& id)
    {
        auto* component = findDescendantWithID (preview, id);
        REQUIRE (component != nullptr);
        auto* button = dynamic_cast<juce::ToggleButton*> (component);
        REQUIRE (button != nullptr);
        return *button;
    }
};
} // namespace

TEST_CASE ("The standalone window has no match controls", "[plugin-match][ui]")
{
    TestDirectory t;
    const auto catalog = t.dir.getChildFile ("standalone.sqlite");
    { Database db (catalog); SampleRepository repo (db); repo.createTables(); }
    MainComponent window (catalog, false, {}, t.dir.getChildFile ("settings.json"));
    CHECK_FALSE (window.matchControlsVisible());
    CHECK (findDescendantWithID (window, "pluginMatchBpm") == nullptr);
}

TEST_CASE ("The plugin places independent match toggles by Play and Stop", "[plugin-match][ui]")
{
    TestDirectory t;
    const auto catalog = t.dir.getChildFile ("plugin.sqlite");
    { Database db (catalog); SampleRepository repo (db); repo.createTables(); }
    plugin::PluginProcessor processor (catalog, t.dir.getChildFile ("settings.json"));
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* main = editor->findChildWithID ("pluginMain");
    REQUIRE (main != nullptr);
    auto* browser = dynamic_cast<MainComponent*> (main);
    REQUIRE (browser != nullptr);
    REQUIRE (browser->matchControlsVisible());

    for (int percent : { 100, 125 })
    {
        main->setTransform (juce::AffineTransform::scale ((float) percent / 100.0f));
        main->setBounds (0, 0, 880, 580);
        main->resized();
        auto* bpm = dynamic_cast<juce::ToggleButton*> (findDescendantWithID (*main, "pluginMatchBpm"));
        auto* scale = dynamic_cast<juce::ToggleButton*> (findDescendantWithID (*main, "pluginMatchScale"));
        auto* play = findDescendantWithID (*main, "previewPlay");
        REQUIRE (bpm != nullptr);
        REQUIRE (scale != nullptr);
        REQUIRE (play != nullptr);
        CHECK (bpm->isVisible());
        CHECK (scale->isVisible());
        CHECK_FALSE (bpm->getToggleState());
        CHECK_FALSE (scale->getToggleState());
        // Same transport group: directly below the Play/Stop row, not over the volume slider.
        auto* row = bpm->getParentComponent();
        REQUIRE (row != nullptr);
        CHECK (row->getY() >= play->getBottom());
        CHECK (row->getX() == play->getX());
    }

    auto* bpm = dynamic_cast<juce::ToggleButton*> (findDescendantWithID (*main, "pluginMatchBpm"));
    auto* scale = dynamic_cast<juce::ToggleButton*> (findDescendantWithID (*main, "pluginMatchScale"));
    press (*bpm);
    CHECK (processor.pluginState().match.matchBpm);
    CHECK_FALSE (processor.pluginState().match.matchScale);
    CHECK_FALSE (scale->getToggleState());
    press (*scale);
    CHECK (processor.pluginState().match.matchBpm);
    CHECK (processor.pluginState().match.matchScale);
}

TEST_CASE ("A restored project sets the toggles without autoplay", "[plugin-match][ui][state]")
{
    TestDirectory t;
    const auto catalog = t.dir.getChildFile ("restore.sqlite");
    { Database db (catalog); SampleRepository repo (db); repo.createTables(); }
    plugin::PluginProcessor processor (catalog, t.dir.getChildFile ("settings.json"));
    plugin::PluginState state;
    state.match.matchScale = true;
    state.match.targetTonic = 9;
    state.match.targetMode = TargetMode::major;
    const auto block = state.toMemoryBlock();
    processor.setStateInformation (block.getData(), (int) block.getSize());

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* main = editor->findChildWithID ("pluginMain");
    REQUIRE (main != nullptr);
    auto* scale = dynamic_cast<juce::ToggleButton*> (findDescendantWithID (*main, "pluginMatchScale"));
    auto* bpm = dynamic_cast<juce::ToggleButton*> (findDescendantWithID (*main, "pluginMatchBpm"));
    auto* tonic = dynamic_cast<juce::ComboBox*> (findDescendantWithID (*main, "pluginMatchTonic"));
    auto* mode = dynamic_cast<juce::ComboBox*> (findDescendantWithID (*main, "pluginMatchMode"));
    REQUIRE (scale != nullptr);
    REQUIRE (bpm != nullptr);
    REQUIRE (tonic != nullptr);
    REQUIRE (mode != nullptr);
    CHECK (scale->getToggleState());
    CHECK_FALSE (bpm->getToggleState());
    CHECK (tonic->getSelectedId() == 10);
    CHECK (mode->getSelectedId() == 2);
    CHECK_FALSE (processor.engine().isPlaying());
}

TEST_CASE ("Match BPM prepares one WAV for the waveform and keeps the source identity",
           "[plugin-match][ui][integration]")
{
    PreviewMatchHarness harness;
    const auto originalModified = harness.sourceFile.getLastModificationTime().toMilliseconds();
    press (harness.toggle ("pluginMatchBpm"));
    REQUIRE (harness.preferences.matchBpm);
    REQUIRE (harness.preview.waitForMatchedPreview (8000));

    CHECK (harness.preview.matchedPreviewActive());
    CHECK (harness.preview.matchedPreviewFile().existsAsFile());
    CHECK (harness.preview.matchedPreviewFile().isAChildOf (harness.t.dir.getChildFile ("exports")));
    CHECK (harness.preview.loadedFilePath() == harness.sourceFile);
    CHECK (harness.preview.waveformLength() == Catch::Approx (7.5).margin (0.01));
    CHECK (harness.preview.waveformLength() != Catch::Approx (8.0));
    CHECK_FALSE (harness.preview.sourceIsEditable());
    CHECK (harness.engine.hasMatchedTransport());
    CHECK (harness.sourceFile.getLastModificationTime().toMilliseconds() == originalModified);

    press (harness.toggle ("pluginMatchBpm"));
    CHECK_FALSE (harness.preview.matchedPreviewActive());
    CHECK_FALSE (harness.engine.hasMatchedTransport());
    CHECK (harness.preview.waveformLength() == Catch::Approx (8.0).margin (0.01));
    CHECK (harness.preview.sourceIsEditable());
}

TEST_CASE ("Match Scale transposes in the same length and refuses a mode conversion",
           "[plugin-match][ui][integration]")
{
    PreviewMatchHarness harness;
    harness.preferences.matchScale = true;
    harness.preferences.targetTonic = 0; // C minor
    harness.preferences.targetMode = TargetMode::minor;
    harness.preview.applyPluginMatchPreferences (harness.preferences);
    auto* scale = dynamic_cast<juce::ToggleButton*> (findDescendantWithID (harness.preview, "pluginMatchScale"));
    REQUIRE (scale != nullptr);
    REQUIRE (harness.preferences.matchScale);
    REQUIRE (harness.preview.waitForMatchedPreview (8000));
    CHECK (harness.preview.matchedPreviewActive());
    CHECK (harness.preview.waveformLength() == Catch::Approx (8.0).margin (0.01));
    auto* status = dynamic_cast<juce::Label*> (findDescendantWithID (harness.preview, "pluginMatchStatus"));
    REQUIRE (status != nullptr);
    CHECK (status->getText().contains ("+3 st"));

    // A minor to C major is not a pitch shift; the panel says so and plays no result.
    auto* mode = dynamic_cast<juce::ComboBox*> (findDescendantWithID (harness.preview, "pluginMatchMode"));
    REQUIRE (mode != nullptr);
    mode->setSelectedId (2, juce::sendNotificationSync);
    harness.preferences.targetMode = TargetMode::major;
    harness.preview.applyPluginMatchPreferences (harness.preferences);
    harness.preview.waitForMatchedPreview (3000);
    CHECK_FALSE (harness.preview.matchedPreviewActive());
    CHECK (status->getText().contains ("Mode mismatch"));
}

TEST_CASE ("Stop during preparation prevents Auto Preview from starting", "[plugin-match][ui][integration]")
{
    PreviewMatchHarness harness;
    REQUIRE (harness.preview.autoPreviewEnabled());
    press (harness.toggle ("pluginMatchBpm"));
    harness.preview.stopPlayback();
    REQUIRE (harness.preview.waitForMatchedPreview (8000));
    CHECK (harness.preview.matchedPreviewActive());
    CHECK_FALSE (harness.engine.isPlaying());
}

TEST_CASE ("A matched transport keeps the original source loaded for MIDI", "[plugin-match][engine]")
{
    TestDirectory t;
    const auto sourceFile = writeLoop (t.dir.getChildFile ("original.wav"), 8.0);
    const auto prepared = writeLoop (t.dir.getChildFile ("prepared.wav"), 7.5);
    AudioEngine engine (false);
    REQUIRE (engine.loadFile (sourceFile));
    const auto originalLength = engine.length();
    CHECK (originalLength == Catch::Approx (8.0).margin (0.01));
    REQUIRE (engine.setMatchedTransport (prepared));
    CHECK (engine.hasMatchedTransport());
    CHECK (engine.length() == Catch::Approx (7.5).margin (0.01));
    engine.play();
    engine.seek (1.0);
    CHECK (engine.position() == Catch::Approx (1.0).margin (0.01));
    engine.clearMatchedTransport();
    CHECK_FALSE (engine.hasMatchedTransport());
    CHECK (engine.length() == Catch::Approx (8.0).margin (0.01));
}
