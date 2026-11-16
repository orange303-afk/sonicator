#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/HostTempoSnapshot.h"
#include "plugin/PluginProcessor.h"
#include "plugin/PluginState.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::plugin;

namespace
{
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    bool hasPlayHead = true;
    bool hasPosition = true;
    bool hasTempo = true;
    bool playing = true;
    double bpm = 120.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! hasPlayHead || ! hasPosition) return {};
        PositionInfo position;
        position.setIsPlaying (playing);
        if (hasTempo) position.setBpm (bpm);
        return position;
    }
};

struct PluginFixture
{
    TestDirectory directory;
    juce::File catalog = directory.dir.getChildFile ("match.sqlite");
    juce::File settings = directory.dir.getChildFile ("settings.json");

    PluginFixture()
    {
        Database db (catalog);
        SampleRepository repo (db);
        repo.createTables();
    }

    std::unique_ptr<PluginProcessor> processor()
    {
        return std::make_unique<PluginProcessor> (catalog, settings);
    }
};

void processOneBlock (PluginProcessor& processor)
{
    processor.prepareToPlay (44100.0, 64);
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
}
} // namespace

TEST_CASE ("Host tempo snapshot keeps validity and revision in one word", "[plugin-match][tempo]")
{
    HostTempoSnapshot snapshot;
    auto value = snapshot.read();
    CHECK_FALSE (value.valid);
    CHECK (value.revision == 0);

    snapshot.publish (120.0);
    value = snapshot.read();
    CHECK (value.valid);
    CHECK (value.bpm == Catch::Approx (120.0));
    CHECK (value.revision == 1);

    snapshot.publish (128.25);
    value = snapshot.read();
    CHECK (value.valid);
    CHECK (value.bpm == Catch::Approx (128.25));
    CHECK (value.revision == 2);

    snapshot.publish (std::nullopt);
    value = snapshot.read();
    CHECK_FALSE (value.valid);
    CHECK (value.bpm == 0.0);
    CHECK (value.revision == 3);

    snapshot.publish (0.0);
    CHECK_FALSE (snapshot.read().valid);
    snapshot.publish (-12.0);
    CHECK_FALSE (snapshot.read().valid);
    snapshot.publish (std::nan (""));
    CHECK_FALSE (snapshot.read().valid);
}

TEST_CASE ("A version 1 project loads with match off", "[plugin-match][state]")
{
    const auto v1 = juce::String ("<SONICATOR_PLUGIN version=\"1\" lastFolder=\"/x\""
                                  " lastSamplePath=\"/x/a.wav\" width=\"1024\" height=\"768\"/>");
    const auto state = PluginState::fromMemoryBlock (v1.toRawUTF8(), v1.getNumBytesAsUTF8());
    CHECK (state.version == 1);
    CHECK (state.windowWidth == 1024);
    CHECK_FALSE (state.match.matchBpm);
    CHECK_FALSE (state.match.matchScale);
    CHECK (state.match.targetTonic == 0);
    CHECK (state.match.targetMode == TargetMode::minor);
}

TEST_CASE ("Match preferences round-trip in every flag combination", "[plugin-match][state]")
{
    for (bool bpm : { false, true })
    {
        for (bool scale : { false, true })
        {
            PluginState state;
            state.match.matchBpm = bpm;
            state.match.matchScale = scale;
            state.match.targetTonic = 7;
            state.match.targetMode = TargetMode::major;
            const auto block = state.toMemoryBlock();
            const auto restored = PluginState::fromMemoryBlock (block.getData(), block.getSize());
            CHECK (restored.version == PluginState::currentVersion);
            CHECK (restored.match.matchBpm == bpm);
            CHECK (restored.match.matchScale == scale);
            CHECK (restored.match.targetTonic == 7);
            CHECK (restored.match.targetMode == TargetMode::major);
        }
    }
}

TEST_CASE ("Malformed match values are clamped instead of trusted", "[plugin-match][state]")
{
    const auto malformed = juce::String ("<SONICATOR_PLUGIN version=\"2\" width=\"1024\" height=\"768\""
                                         " matchBpm=\"1\" matchScale=\"1\" targetTonic=\"44\""
                                         " targetMode=\"9\"/>");
    const auto state = PluginState::fromMemoryBlock (malformed.toRawUTF8(), malformed.getNumBytesAsUTF8());
    CHECK (state.match.targetTonic == 11);
    CHECK (state.match.targetMode == TargetMode::minor);
    CHECK (state.match.matchBpm);
    CHECK (state.match.matchScale);

    juce::MemoryBlock oversized (PluginState::maximumStateBytes + 1, true);
    const auto ignored = PluginState::fromMemoryBlock (oversized.getData(), oversized.getSize());
    CHECK_FALSE (ignored.match.matchBpm);
    CHECK (ignored.windowWidth == PluginState::defaultWidth);
}

TEST_CASE ("Two instances keep independent match intent", "[plugin-match][state]")
{
    PluginFixture fixture;
    auto first = fixture.processor();
    auto second = fixture.processor();
    PluginMatchPreferences prefs;
    prefs.matchBpm = true;
    prefs.matchScale = true;
    prefs.targetTonic = 3;
    prefs.targetMode = TargetMode::major;
    first->setMatchPreferences (prefs);
    CHECK (first->pluginState().match.matchBpm);
    CHECK_FALSE (second->pluginState().match.matchBpm);
    CHECK (second->pluginState().match.targetTonic == 0);
}

TEST_CASE ("Restored project state carries the match intent", "[plugin-match][state]")
{
    PluginFixture fixture;
    auto processor = fixture.processor();
    PluginState state;
    state.match.matchScale = true;
    state.match.targetTonic = 9;
    state.match.targetMode = TargetMode::major;
    const auto block = state.toMemoryBlock();
    processor->setStateInformation (block.getData(), (int) block.getSize());
    CHECK (processor->pluginState().match.matchScale);
    CHECK (processor->pluginState().match.targetTonic == 9);
    CHECK (processor->pluginState().match.targetMode == TargetMode::major);
}

TEST_CASE ("Host tempo follows the playhead and never persists", "[plugin-match][tempo]")
{
    PluginFixture fixture;
    auto processor = fixture.processor();
    FakePlayHead playHead;
    processor->setPlayHead (&playHead);

    processOneBlock (*processor);
    CHECK (processor->hostTempo().valid);
    CHECK (processor->hostTempo().bpm == Catch::Approx (120.0));

    playHead.bpm = 128.0;
    processOneBlock (*processor);
    CHECK (processor->hostTempo().bpm == Catch::Approx (128.0));

    // A stopped transport still reports the project tempo.
    playHead.playing = false;
    playHead.bpm = 90.0;
    processOneBlock (*processor);
    CHECK (processor->hostTempo().valid);
    CHECK (processor->hostTempo().bpm == Catch::Approx (90.0));

    // A missing position is not proof that the tempo disappeared.
    playHead.hasPosition = false;
    processOneBlock (*processor);
    CHECK (processor->hostTempo().valid);
    CHECK (processor->hostTempo().bpm == Catch::Approx (90.0));

    // An explicit invalid tempo is: no fake fallback.
    playHead.hasPosition = true;
    playHead.bpm = 0.0;
    processOneBlock (*processor);
    CHECK_FALSE (processor->hostTempo().valid);

    playHead.bpm = 128.0;
    processOneBlock (*processor);
    CHECK (processor->hostTempo().valid);

    // Release invalidates the snapshot.
    processor->releaseResources();
    CHECK_FALSE (processor->hostTempo().valid);

    // Tempo is host context, never project state.
    const auto state = processor->pluginState();
    CHECK (state.match.targetTonic >= 0);
    const auto block = state.toMemoryBlock();
    const auto text = juce::String::fromUTF8 (static_cast<const char*> (block.getData()),
                                              (int) block.getSize());
    CHECK_FALSE (text.contains ("128")); // the host tempo itself is not stored
}

TEST_CASE ("A host without a playhead has no tempo", "[plugin-match][tempo]")
{
    PluginFixture fixture;
    auto processor = fixture.processor();
    FakePlayHead playHead;
    processor->setPlayHead (&playHead);
    processOneBlock (*processor);
    REQUIRE (processor->hostTempo().valid);

    processor->setPlayHead (nullptr);
    processOneBlock (*processor);
    CHECK_FALSE (processor->hostTempo().valid);
}
