#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Audio preview actually renders, resamples, loops and stops", "[audio]")
{
    TestDirectory t;
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(t.wave()));
    REQUIRE_FALSE(engine.isPlaying());
    engine.transport().prepareToPlay(256, 48000);
    REQUIRE(engine.length() == Catch::Approx(1.0).margin(0.001));
    engine.setVolume(0.5f);
    engine.play();
    juce::AudioBuffer<float> buffer(2,256);
    for (int i = 0; i < 4; ++i) engine.transport().getNextAudioBlock({&buffer,0,256});
    REQUIRE(buffer.getMagnitude(0,256) > 0.3f);
    REQUIRE(buffer.getMagnitude(0,256) < 0.41f);
    REQUIRE(engine.position() > 0.01);
    engine.seek(-10); REQUIRE(engine.position() == 0);
    engine.seek(999); REQUIRE(engine.position() == Catch::Approx(1).margin(0.001));
    engine.setLooping(true); engine.seek(0.99); engine.play();
    for (int i = 0; i < 8; ++i) engine.transport().getNextAudioBlock({&buffer,0,256});
    REQUIRE(engine.isPlaying());
    REQUIRE(engine.position() < 0.1);
    engine.stop();
    REQUIRE_FALSE(engine.isPlaying()); REQUIRE(engine.position() == 0);
    engine.setVolume(-1); REQUIRE(engine.getVolume() == 0);
    engine.setVolume(5); REQUIRE(engine.getVolume() == 1);
    REQUIRE_FALSE(engine.loadFile(t.dir.getChildFile("absent.wav")));
    REQUIRE(engine.error().isNotEmpty());
    REQUIRE(engine.length() == 0);
}
