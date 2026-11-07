#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/Rex2Metadata.h"
#include "core/rex/Rex2SlicePlayer.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("REX marker-only first slice occupies the first MIDI slot", "[rex2][slices]")
{
    const std::vector<Rex2Slice> markers { { 1, 1, 0 }, { 15892, 23872, 2 },
                                         { 39764, 7955, 2 } };
    const auto playable = playableRex2Slices(markers, 47719);
    REQUIRE(playable.size() == 3);
    REQUIRE(playable[0].start == 1);
    REQUIRE(playable[0].length == 15891);
    REQUIRE(playable[1].start == 15892);
    REQUIRE(playable[1].length == 23872);
}
TEST_CASE("REX marker ranges remain ordered and bounded", "[rex2][slices]")
{
    const std::vector<Rex2Slice> raw { { 300, 1, 0 }, { 100, 1, 0 },
                                      { 200, 50, 1 }, { 500, 1, 0 } };
    const auto playable = playableRex2Slices(raw, 400);
    REQUIRE(playable.size() == 2);
    REQUIRE(playable[0].start == 100);
    REQUIRE(playable[0].length == 100); // stop at the muted slice, do not merge it
    REQUIRE(playable[1].start == 300);
    REQUIRE(playable[1].length == 100); // last marker ends at the waveform boundary
}
namespace {
struct Be
{
    std::vector<juce::uint8> v;
    void ascii(const juce::String& s) { v.insert(v.end(), s.toRawUTF8(), s.toRawUTF8() + s.getNumBytesAsUTF8()); }
    void u16be(juce::uint16 x) { v.push_back(x >> 8); v.push_back(x & 0xff); }
    void u32be(juce::uint32 x) { for (int i = 3; i >= 0; --i) v.push_back((x >> (8 * i)) & 0xff); }
};
void chunk(Be& out, const char* id, Be payload)
{
    out.ascii(id); out.u32be((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
Be globChunk(int slices, double previewTempo)
{
    Be b;
    b.u32be((juce::uint32) slices); b.u16be(1); b.v.push_back(0); b.v.push_back(4); b.v.push_back(4);
    b.v.push_back(0x4e); b.u16be(0); b.u16be(1200); b.u16be(1);
    b.u32be((juce::uint32) std::lround(previewTempo * 1000.0));
    b.v.push_back(1); b.v.push_back(0);
    return b;
}
Be sinfChunk(int channels, int rate, juce::uint32 frames)
{
    Be b; b.v.push_back((juce::uint8) channels); b.v.push_back(3); b.u32be((juce::uint32) rate);
    b.u32be(frames); b.u32be(0); b.u32be(frames);
    return b;
}
Be sliceChunk(juce::uint32 start, juce::uint32 length, int flags)
{
    Be b; b.u32be(start); b.u32be(length); b.u16be(0x7fff); b.v.push_back((juce::uint8) flags);
    return b;
}
juce::File writeRex(TestDirectory& t, const juce::String& name, Be body)
{
    Be file; file.ascii("CAT "); file.u32be((juce::uint32) body.v.size() + 4); file.ascii("REX2");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name); path.deleteFile();
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
const SmartCandidate* find(const Rex2MetadataResult& result, const juce::String& source)
{
    for (const auto& c : result.candidates) if (c.source == source) return &c;
    return nullptr;
}
}
TEST_CASE("REX2 GLOB tempo and slice boundaries are read", "[smart][rex2]")
{
    TestDirectory t;
    Be slices; slices.ascii("SLCL");
    chunk(slices, "SLCE", sliceChunk(0, 1000, 0));
    chunk(slices, "SLCE", sliceChunk(1000, 1000, 0));
    chunk(slices, "SLCE", sliceChunk(2000, 1000, 1)); // muted
    Be body;
    chunk(body, "GLOB", globChunk(3, 140.0));
    chunk(body, "SINF", sinfChunk(2, 44100, 3000));
    chunk(body, "CAT ", slices);
    auto result = extractRex2Metadata(writeRex(t, "loop.rx2", body));
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.info.previewTempo == Catch::Approx(140.0));
    REQUIRE(result.info.channels == 2);
    REQUIRE(result.info.sampleRate == 44100);
    REQUIRE(result.info.frames == 3000);
    REQUIRE(result.info.slices.size() == 3);
    REQUIRE(result.playableSlices == 2);
    REQUIRE(result.mutedSlices == 1);
    auto* bpm = find(result, "rx2_glob");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->numericValue == Catch::Approx(140.0));
    REQUIRE(bpm->status == CandidateStatus::Valid);
}
TEST_CASE("Slice bounds beyond the decoded waveform are rejected", "[smart][rex2]")
{
    TestDirectory t;
    Be slices; slices.ascii("SLCL");
    chunk(slices, "SLCE", sliceChunk(0, 1000, 0));
    chunk(slices, "SLCE", sliceChunk(900, 500, 0)); // 1400 > 1000 frames
    Be body;
    chunk(body, "SINF", sinfChunk(1, 44100, 1000));
    chunk(body, "CAT ", slices);
    auto result = extractRex2Metadata(writeRex(t, "bad.rx2", body));
    REQUIRE(result.playableSlices == 1);
    REQUIRE(result.badSlices == 1);
}
TEST_CASE("RECY original tempo is preferred metadata when present", "[smart][rex2]")
{
    TestDirectory t;
    Be recy; for (int i = 0; i < 8; ++i) recy.v.push_back(0); recy.u32be(128000); recy.u16be(0); recy.v.push_back(8);
    Be body;
    chunk(body, "GLOB", globChunk(0, 130.0));
    chunk(body, "RECY", recy);
    chunk(body, "SINF", sinfChunk(1, 44100, 1000));
    auto result = extractRex2Metadata(writeRex(t, "recy.rx2", body));
    REQUIRE(result.info.originalTempo == Catch::Approx(128.0));
    REQUIRE(find(result, "rx2_recy") != nullptr);
}
TEST_CASE("A lying REX2 chunk size stops parsing safely", "[smart][rex2]")
{
    TestDirectory t;
    Be body;
    body.ascii("GLOB"); body.u32be(0x7fffffff);
    auto result = extractRex2Metadata(writeRex(t, "truncated.rx2", body));
    REQUIRE(result.error.contains("exceeds"));
    REQUIRE(result.truncated);
}
