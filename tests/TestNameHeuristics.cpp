#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/NameHeuristics.h"
using namespace sonicator;
namespace {
std::vector<SmartCandidate> only(const std::vector<SmartCandidate>& all, SmartField field)
{
    std::vector<SmartCandidate> out;
    for (const auto& c : all) if (c.field == field) out.push_back(c);
    return out;
}
SmartCandidate first(const std::vector<SmartCandidate>& all, SmartField field)
{
    auto list = only(all, field);
    REQUIRE(! list.empty());
    return list.front();
}
}
TEST_CASE("Explicit BPM tokens are extracted and bare numbers are not", "[smart][names]")
{
    auto bpm = first(extractNameCandidates("/lib/kick_128bpm_Amin.wav"), SmartField::Bpm);
    REQUIRE(bpm.numericValue == Catch::Approx(128.0));
    auto spaced = first(extractNameCandidates("/lib/kick BPM 140.wav"), SmartField::Bpm);
    REQUIRE(spaced.numericValue == Catch::Approx(140.0));
    REQUIRE(only(extractNameCandidates("/lib/kick_140.wav"), SmartField::Bpm).empty());
    REQUIRE(only(extractNameCandidates("/lib/Windows_95_Startup.wav"), SmartField::Bpm).empty());
}
TEST_CASE("Key spelling variants normalize", "[smart][names]")
{
    REQUIRE(first(extractNameCandidates("/lib/bass_riff3_keyC#min_105bpm.wav"), SmartField::Key)
                .textValue == "C# minor");
    REQUIRE(first(extractNameCandidates("/lib/EDX_Padline_Bb_Minor_122bpm.wav"), SmartField::Key)
                .textValue == "Bb minor");
    REQUIRE(first(extractNameCandidates("/lib/cw_vocaloid_pad_Emaj.wav"), SmartField::Key)
                .textValue == "E major");
    REQUIRE(first(extractNameCandidates("/lib/VV17_127_F#min_Female.wav"), SmartField::Key)
                .textValue == "F# minor");
}
TEST_CASE("Words that merely contain key-like letters are ignored", "[smart][names]")
{
    REQUIRE(only(extractNameCandidates("/lib/Windows_95_Startup.wav"), SmartField::Key).empty());
    REQUIRE(only(extractNameCandidates("/lib/Amen_break.wav"), SmartField::Key).empty());
    REQUIRE(only(extractNameCandidates("/lib/album_art.wav"), SmartField::Key).empty());
    REQUIRE(only(extractNameCandidates("/lib/drum_loop.wav"), SmartField::Key).empty());
}
TEST_CASE("Type and structure use exact tokens", "[smart][names]")
{
    auto kick = extractNameCandidates("/lib/PTV1_Kick_15.wav");
    REQUIRE(first(kick, SmartField::Type).textValue == "kick");
    // Drum abbreviations are whole tokens, the same table AutoTag uses; a numeric index
    // glued to the abbreviation ("bd07") is split off before matching.
    REQUIRE(first(extractNameCandidates("/lib/technox_bd07.wav"), SmartField::Type).textValue == "kick");
    REQUIRE(first(extractNameCandidates("/lib/SD_MPC60_Vinnie_009.wav"), SmartField::Type).textValue == "snare");
    REQUIRE(first(extractNameCandidates("/lib/hh_01.wav"), SmartField::Type).textValue == "hat");
    REQUIRE(only(extractNameCandidates("/lib/abdx.wav"), SmartField::Type).empty());
    REQUIRE(only(extractNameCandidates("/lib/bdroom.wav"), SmartField::Type).empty());
    REQUIRE(first(extractNameCandidates("/lib/Uplifting_Vocal_Loop_128bpm.wav"), SmartField::Type)
                .textValue == "vocal");
    auto structure = extractNameCandidates("/lib/One_Shot_Kick.wav");
    REQUIRE(first(structure, SmartField::Structure).textValue == "one-shot");
    REQUIRE(first(extractNameCandidates("/lib/Bass_Loop_120bpm.wav"), SmartField::Structure)
                .textValue == "loop");
}
TEST_CASE("Instrument words match as whole tokens", "[smart][names]")
{
    auto typeOf = [](const juce::String& path)
    {
        const auto list = only(extractNameCandidates(path), SmartField::Type);
        return list.empty() ? juce::String() : list.front().textValue;
    };
    CHECK(typeOf("/lib/piano_01.wav") == "piano");
    CHECK(typeOf("/lib/grand_piano.wav") == "piano");
    CHECK(typeOf("/lib/organ_01.wav") == "organ");
    CHECK(typeOf("/lib/synth.wav") == "synth");
    CHECK(typeOf("/lib/chord_Cmin.wav") == "chord");
    CHECK(typeOf("/lib/stab_02.wav") == "stab");
    CHECK(typeOf("/lib/fx.wav") == "fx");
    CHECK(typeOf("/lib/riser.wav") == "riser");
    CHECK(typeOf("/lib/uplifter.wav") == "riser");
    CHECK(typeOf("/lib/downlifter.wav") == "downfilter");
    CHECK(typeOf("/lib/boom.wav") == "impact");
    CHECK(typeOf("/lib/guitar_riff.wav") == "guitar");
    CHECK(typeOf("/lib/lead_01.wav") == "lead");
    CHECK(typeOf("/lib/bass_01.wav") == "bass");
    CHECK(typeOf("/lib/wobble.wav") == "wobble");
    CHECK(typeOf("/lib/bell.wav") == "bell");
    CHECK(typeOf("/lib/cowbell.wav") == "cowbell");
    CHECK(typeOf("/lib/pad_01.wav") == "pad");
    CHECK(typeOf("/lib/drone.wav") == "drone");
    // A word that merely contains an instrument name is not that instrument.
    CHECK(typeOf("/lib/pianoforte.wav").isEmpty());
    CHECK(typeOf("/lib/organelle.wav").isEmpty());
    CHECK(typeOf("/lib/bellboy.wav").isEmpty());
    CHECK(typeOf("/lib/wobbling.wav").isEmpty());
    CHECK(typeOf("/lib/boomtown.wav").isEmpty());
    CHECK(typeOf("/lib/stability.wav").isEmpty());
}

TEST_CASE("Nearest folder is context with its own source", "[smart][names]")
{
    auto result = extractNameCandidates("/lib/Drums/Kicks/Kick_01.wav");
    bool hasFolderCandidate = false;
    for (const auto& c : result)
        if (c.field == SmartField::Type && c.source == "folder" && c.textValue == "kick")
            hasFolderCandidate = true;
    REQUIRE(hasFolderCandidate);
}
TEST_CASE("The nearest folder that names an instrument is folder evidence", "[smart][names]")
{
    auto folderTypes = [](const juce::String& path)
    {
        juce::StringArray types;
        for (const auto& c : extractNameCandidates(path))
            if (c.field == SmartField::Type && c.source == "folder") types.add(c.textValue);
        return types;
    };
    CHECK(folderTypes("/lib/SNARE/take 01.wav") == juce::StringArray { "snare" });
    CHECK(folderTypes("/lib/BD/take 01.wav") == juce::StringArray { "kick" });
    CHECK(folderTypes("/lib/PAD/take 01.wav") == juce::StringArray { "pad" });
    // A deeper ancestor still names the pack when the immediate folder says nothing.
    CHECK(folderTypes("/lib/Best Service - XXL 800 Pads/Vol 1/01.wav")
          == juce::StringArray { "pad" });
    // Only the nearest folder counts: a "Kicks" root above "SNARE" adds nothing.
    CHECK(folderTypes("/lib/Kicks/SNARE/take 01.wav") == juce::StringArray { "snare" });
    // Numbers from a distant ancestor are not evidence, only its instrument word.
    CHECK(extractNameCandidates("/lib/128 bpm/Pads/01.wav").size()
          == extractNameCandidates("/lib/Pads/01.wav").size());
}

TEST_CASE("Multiple keys in one name are all kept", "[smart][names]")
{
    auto keys = only(extractNameCandidates("/lib/Amin_to_Cmaj_transition.wav"), SmartField::Key);
    REQUIRE(keys.size() >= 2);
}
