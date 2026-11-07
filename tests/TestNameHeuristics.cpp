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
    REQUIRE(only(extractNameCandidates("/lib/SD_MPC60_Vinnie_009.wav"), SmartField::Type).empty());
    REQUIRE(first(extractNameCandidates("/lib/Uplifting_Vocal_Loop_128bpm.wav"), SmartField::Type)
                .textValue == "vocal");
    auto structure = extractNameCandidates("/lib/One_Shot_Kick.wav");
    REQUIRE(first(structure, SmartField::Structure).textValue == "one-shot");
    REQUIRE(first(extractNameCandidates("/lib/Bass_Loop_120bpm.wav"), SmartField::Structure)
                .textValue == "loop");
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
TEST_CASE("Multiple keys in one name are all kept", "[smart][names]")
{
    auto keys = only(extractNameCandidates("/lib/Amin_to_Cmaj_transition.wav"), SmartField::Key);
    REQUIRE(keys.size() >= 2);
}
