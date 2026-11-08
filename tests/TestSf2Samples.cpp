#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/sf2/Sf2Bank.h"
#include "Sf2TestBank.h"
using namespace sonicator;
using namespace sf2test;
TEST_CASE("SF2 preset lists unique used samples and excludes unused headers", "[sf2][samples]")
{
    TestDirectory t;
    auto path = writeBank(t,
        { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 },
        { { "kick", 0, 4, 0, 0, 44100 }, { "kick", 4, 8, 0, 0, 44100 }, { "unused", 0, 2, 0, 0, 44100 } },
        { { "Kit", { { 36, 36, 0, 63, 0 }, { 36, 36, 64, 127, 1 }, { 38, 38, 0, 127, 0 } } } },
        { { "Drums", 128, 0, { { 0 } } } });
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    const auto used = bank.usedSamples(0, error);
    REQUIRE(error.isEmpty());
    REQUIRE(used.size() == 2);
    REQUIRE(used[0].id == 0);
    REQUIRE(used[0].name == "kick");
    REQUIRE(used[0].frameCount == 4);
    REQUIRE(used[0].zones.size() == 2);
    REQUIRE(used[0].zones[0].velocityLow == 0);
    REQUIRE(used[0].zones[0].velocityHigh == 63);
    REQUIRE(used[0].zones[1].keyLow == 38);
    REQUIRE(used[1].id == 1);
    REQUIRE(used[1].name == "kick");
    REQUIRE(used[1].zones.size() == 1);
    REQUIRE(used[1].zones[0].velocityLow == 64);
    REQUIRE(used[1].isPlayable());
}
TEST_CASE("SF2 sample audio exposes exact mono frames and loop bounds", "[sf2][samples]")
{
    TestDirectory t;
    auto path = writeBank(t,
        { 1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000 },
        { { "tone", 0, 8, 2, 6, 48000 } },
        { { "Inst", { { 0, 127, 0, 127, 0 } } } },
        { { "Preset", 0, 0, { { 0 } } } });
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    const auto used = bank.usedSamples(0, error);
    REQUIRE(used.size() == 1);
    REQUIRE(used[0].sampleRate == 48000);
    REQUIRE(used[0].channels == 1);
    REQUIRE(used[0].frameCount == 8);
    REQUIRE(used[0].loopStart == 2);
    REQUIRE(used[0].loopEnd == 6);
    REQUIRE(used[0].looping);
    auto audio = bank.sampleAudio(used[0].id, error);
    REQUIRE(error.isEmpty());
    REQUIRE(audio != nullptr);
    REQUIRE(audio->sampleRate == 48000);
    REQUIRE(audio->channels == 1);
    REQUIRE(audio->frames == 8);
    REQUIRE(audio->left.size() == 8);
    REQUIRE(audio->right.empty());
    REQUIRE(audio->left[0] == Catch::Approx(1000.0 / 32768.0));
    REQUIRE(audio->left[1] == Catch::Approx(-2000.0 / 32768.0));
    REQUIRE(audio->left[7] == Catch::Approx(-8000.0 / 32768.0));
    REQUIRE(audio->sampleAt(0, 2) == Catch::Approx(3000.0 / 32768.0));
    REQUIRE(audio->sampleAt(0, -1) == 0.0f);
    REQUIRE(audio->sampleAt(0, 8) == 0.0f);
}
TEST_CASE("SF2 stereo pair is one descriptor with two aligned channels", "[sf2][samples]")
{
    TestDirectory t;
    auto path = writeBank(t,
        { 1000, 2000, 3000, 4000, -1000, -2000, -3000, -4000 },
        { { "left", 0, 4, 0, 0, 44100, 60, 0, 1, 4 },
          { "right", 4, 8, 0, 0, 44100, 60, 0, 0, 2 } },
        { { "Inst", { { 0, 127, 0, 127, 0 }, { 0, 127, 0, 127, 1 } } } },
        { { "Preset", 0, 0, { { 0 } } } });
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    const auto used = bank.usedSamples(0, error);
    REQUIRE(used.size() == 1);
    REQUIRE(used[0].id == 0);
    REQUIRE(used[0].channels == 2);
    REQUIRE(used[0].frameCount == 4);
    REQUIRE(used[0].zones.size() == 2);
    auto audio = bank.sampleAudio(used[0].id, error);
    REQUIRE(error.isEmpty());
    REQUIRE(audio != nullptr);
    REQUIRE(audio->channels == 2);
    REQUIRE(audio->frames == 4);
    REQUIRE(audio->left.size() == 4);
    REQUIRE(audio->right.size() == 4);
    REQUIRE(audio->left[0] == Catch::Approx(1000.0 / 32768.0));
    REQUIRE(audio->right[0] == Catch::Approx(-1000.0 / 32768.0));
    REQUIRE(audio->right[3] == Catch::Approx(-4000.0 / 32768.0));
}
TEST_CASE("SF2 malformed sample bounds and stereo links report an error", "[sf2][samples]")
{
    TestDirectory t;
    auto path = writeBank(t,
        { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 },
        { { "reversed", 100, 50, 0, 0, 44100 },
          { "badlink", 0, 4, 0, 0, 44100, 60, 0, 999, 4 },
          { "clamped", 4, 8, 6, 99, 44100 } },
        { { "Inst", { { 0, 127, 0, 127, 0 }, { 0, 127, 0, 127, 1 }, { 0, 127, 0, 127, 2 } } } },
        { { "Preset", 0, 0, { { 0 } } } });
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(path, error));
    const auto used = bank.usedSamples(0, error);
    REQUIRE(used.size() == 3);
    REQUIRE(used[0].error.isNotEmpty());
    REQUIRE_FALSE(used[0].isPlayable());
    REQUIRE(used[1].error.isNotEmpty());
    REQUIRE_FALSE(used[1].isPlayable());
    REQUIRE(used[2].error.isEmpty());
    REQUIRE(used[2].loopStart == 2);
    REQUIRE(used[2].loopEnd == 4);
    REQUIRE(used[2].looping);
    REQUIRE(bank.sampleAudio(used[0].id, error) == nullptr);
    REQUIRE(error.isNotEmpty());
    error.clear();
    REQUIRE(bank.sampleAudio(used[1].id, error) == nullptr);
    REQUIRE(error.isNotEmpty());
    error.clear();
    auto clamped = bank.sampleAudio(used[2].id, error);
    REQUIRE(error.isEmpty());
    REQUIRE(clamped != nullptr);
    REQUIRE(clamped->frames == 4);
    REQUIRE(clamped->left[0] == Catch::Approx(5000.0 / 32768.0));
}
TEST_CASE("A retained SF2 sample snapshot stays valid after the bank changes", "[sf2][samples]")
{
    TestDirectory t;
    auto first = writeBank(t,
        { 1000, 2000, 3000, 4000 },
        { { "held", 0, 4, 0, 0, 44100 } },
        { { "Inst", { { 0, 127, 0, 127, 0 } } } },
        { { "Preset", 0, 0, { { 0 } } } });
    Sf2Bank bank; juce::String error;
    REQUIRE(bank.load(first, error));
    auto audio = bank.sampleAudio(0, error);
    REQUIRE(audio != nullptr);
    bank.clear();
    REQUIRE_FALSE(bank.isLoaded());
    REQUIRE(audio->frames == 4);
    REQUIRE(audio->left[3] == Catch::Approx(4000.0 / 32768.0));
    auto second = t.dir.getChildFile("second.sf2");
    REQUIRE(second.replaceWithText("not a soundfont"));
    REQUIRE_FALSE(bank.load(second, error));
    REQUIRE(audio->left[0] == Catch::Approx(1000.0 / 32768.0));
}
