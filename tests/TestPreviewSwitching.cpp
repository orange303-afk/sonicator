#include <catch2/catch_test_macros.hpp>
#include "core/audio/AudioEngine.h"
#include "TestHelpers.h"

using namespace sonicator;

TEST_CASE("Switching to a decoded source stops the ordinary file transport", "[preview][switch]")
{
    TestDirectory t;
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(t.wave("long.wav")));
    engine.play();
    REQUIRE(engine.isPlaying());
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = 44100;
    audio->channels = 1;
    audio->frames = 44100;
    audio->left.assign(44100, 0.1f);
    REQUIRE(engine.setDiscSample(audio, false, 0, 0, false));
    // The previous file must not keep sounding under the new source.
    REQUIRE_FALSE(engine.transport().isPlaying());
}

TEST_CASE("Clearing a decoded source stops the ordinary file transport", "[preview][switch]")
{
    TestDirectory t;
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(t.wave("long.wav")));
    engine.play();
    REQUIRE(engine.isPlaying());
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = 44100;
    audio->channels = 1;
    audio->frames = 44100;
    audio->left.assign(44100, 0.1f);
    REQUIRE(engine.setDiscSample(audio, false, 0, 0, false));
    engine.loadFile(t.wave("next.wav"));
    REQUIRE_FALSE(engine.transport().isPlaying());
}
