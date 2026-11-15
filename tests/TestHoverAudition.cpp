#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "core/audio/HoverAudition.h"
#include "TestHelpers.h"
#include <cmath>

using namespace sonicator;

namespace
{
HoverAudition::ClipPtr constantClip(float value, int frames, double rate = 44100.0)
{
    auto clip = std::make_shared<HoverAudition::Clip>();
    clip->buffer.setSize(1, frames);
    clip->buffer.clear();
    for (int i = 0; i < frames; ++i) clip->buffer.setSample(0, i, value);
    clip->sampleRate = rate;
    clip->frames = frames;
    return clip;
}

float blockPeak(HoverAudition& hover, int frames, float& first, float& last)
{
    juce::AudioBuffer<float> out(1, frames);
    out.clear();
    hover.render(out, 0, frames);
    first = out.getSample(0, 0);
    last = out.getSample(0, frames - 1);
    return out.getMagnitude(0, frames);
}
} // namespace

TEST_CASE("Hover audition ramps in and reaches the clip level", "[cloud][hover]")
{
    HoverAudition hover;
    hover.prepare(44100.0);
    hover.setAudio(constantClip(0.5f, 44100));
    hover.play();
    float first = 0, last = 0;
    blockPeak(hover, 512, first, last);
    REQUIRE(std::abs(first) < 0.01f);          // no click at the start
    REQUIRE(last == Catch::Approx(0.5f).margin(0.01f)); // ramp is done inside 10 ms
    REQUIRE(hover.isPlaying());
}

TEST_CASE("Hover audition fades out without a click and then falls silent", "[cloud][hover]")
{
    HoverAudition hover;
    hover.prepare(44100.0);
    hover.setAudio(constantClip(0.5f, 44100));
    hover.play();
    float first = 0, last = 0;
    blockPeak(hover, 512, first, last);
    hover.stop();
    juce::AudioBuffer<float> out(1, 1000);
    out.clear();
    hover.render(out, 0, 1000);
    REQUIRE(out.getMagnitude(500, 500) == 0.0f); // the ramp is over well inside this block
    REQUIRE_FALSE(hover.isPlaying());
}

TEST_CASE("Replaying the same clip ramps in again", "[cloud][hover]")
{
    HoverAudition hover;
    hover.prepare(44100.0);
    hover.setAudio(constantClip(0.5f, 3000));
    hover.play();
    juce::AudioBuffer<float> out(1, 4000);
    out.clear();
    hover.render(out, 0, 4000); // runs to the end of the clip
    REQUIRE_FALSE(hover.isPlaying());
    hover.play();
    out.clear();
    hover.render(out, 0, 32);
    REQUIRE(std::abs(out.getSample(0, 0)) < 0.01f);
}

TEST_CASE("Swapping clips while playing starts from silence", "[cloud][hover]")
{
    HoverAudition hover;
    hover.prepare(44100.0);
    hover.setAudio(constantClip(0.5f, 44100));
    hover.play();
    float first = 0, last = 0;
    blockPeak(hover, 512, first, last);
    hover.setAudio(constantClip(0.8f, 44100));
    REQUIRE_FALSE(hover.isPlaying());
    hover.play();
    juce::AudioBuffer<float> out(1, 64);
    out.clear();
    hover.render(out, 0, 64);
    REQUIRE(std::abs(out.getSample(0, 0)) < 0.01f);
    REQUIRE(out.getMagnitude(0, 8) < 0.03f);
}

TEST_CASE("Hover clips are bounded to the requested seconds", "[cloud][hover]")
{
    TestDirectory t;
    const auto file = t.wave("long.wav", 220.0, 1, 44100 * 5);
    auto clip = HoverAudition::load(file, 1.0);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->frames <= 44100 + 512);
    REQUIRE(clip->frames > 0);
    REQUIRE(clip->sampleRate == 44100.0);

    TestDirectory other;
    const auto missing = other.dir.getChildFile("nope.wav");
    REQUIRE(HoverAudition::load(missing) == nullptr);
}
