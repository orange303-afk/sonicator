#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/AiffMetadata.h"
#include "TestHelpers.h"
#include <cmath>
using namespace sonicator;
namespace {
struct Big
{
    std::vector<juce::uint8> v;
    void ascii(const juce::String& text) { v.insert(v.end(), text.toRawUTF8(), text.toRawUTF8() + text.getNumBytesAsUTF8()); }
    void u16be(juce::uint16 value) { v.push_back(value >> 8); v.push_back(value & 0xff); }
    void u32be(juce::uint32 value) { for (int i = 3; i >= 0; --i) v.push_back((value >> (8 * i)) & 0xff); }
    void ext80(double value)
    {
        if (value == 0) { for (int i = 0; i < 10; ++i) v.push_back(0); return; }
        int exponent = 0;
        const double mantissa = std::frexp(value, &exponent);
        const auto field = (juce::uint16) (exponent - 1 + 16383);
        v.push_back((field >> 8) & 0xff); v.push_back(field & 0xff);
        const juce::uint64 bits = (juce::uint64) std::ldexp(mantissa, 64);
        for (int i = 7; i >= 0; --i) v.push_back((bits >> (8 * i)) & 0xff);
    }
};
void chunk(Big& out, const char* id, Big payload)
{
    out.ascii(id); out.u32be((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
Big commChunk(int channels, juce::uint32 frames, int bits, double rate)
{
    Big b; b.u16be((juce::uint16) channels); b.u32be(frames); b.u16be((juce::uint16) bits); b.ext80(rate);
    return b;
}
Big bascChunk(juce::uint32 version, juce::uint32 beats, int root, int scale)
{
    Big b; b.u32be(version); b.u32be(beats); b.u16be((juce::uint16) root);
    b.u16be((juce::uint16) scale); b.u16be(4); b.u16be(4); b.u16be(1);
    return b;
}
juce::File writeAiff(TestDirectory& t, const juce::String& name, Big body)
{
    Big file; file.ascii("FORM"); file.u32be((juce::uint32) body.v.size() + 4); file.ascii("AIFF");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name); path.deleteFile();
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
const SmartCandidate* findCandidate(const AiffMetadataResult& result, SmartField field,
                                    const juce::String& source)
{
    for (const auto& c : result.candidates)
        if (c.field == field && c.source == source) return &c;
    return nullptr;
}
}
TEST_CASE("Apple Loops basc derives BPM from beats and real duration", "[smart][aiff]")
{
    TestDirectory t;
    Big body;
    chunk(body, "COMM", commChunk(2, 176400, 16, 44100.0)); // 4 seconds
    chunk(body, "basc", bascChunk(1, 8, 55, 3));
    Big cate; cate.u32be(1); cate.ascii("Drums"); cate.v.push_back(0);
    chunk(body, "cate", cate);
    auto result = extractAiffMetadata(writeAiff(t, "loop.aif", body));
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.info.duration == Catch::Approx(4.0).margin(1e-6));
    auto* bpm = findCandidate(result, SmartField::Bpm, "aiff_basc");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->numericValue == Catch::Approx(120.0).margin(1e-6));
    auto* root = findCandidate(result, SmartField::RootNote, "aiff_basc");
    REQUIRE(root != nullptr);
    REQUIRE(root->numericValue == 55);
    REQUIRE(findCandidate(result, SmartField::Key, "aiff_basc") == nullptr);
    auto* type = findCandidate(result, SmartField::Type, "aiff_cate");
    REQUIRE(type != nullptr);
    REQUIRE(type->textValue == "percussion");
}
TEST_CASE("Unspecified Apple Loops root does not create a root candidate", "[smart][aiff]")
{
    TestDirectory t;
    Big body;
    chunk(body, "COMM", commChunk(1, 44100, 16, 44100.0));
    chunk(body, "basc", bascChunk(1, 4, 0, 3));
    auto result = extractAiffMetadata(writeAiff(t, "noroot.aif", body));
    REQUIRE(findCandidate(result, SmartField::RootNote, "aiff_basc") == nullptr);
}
TEST_CASE("Unknown basc version is reported, never guessed", "[smart][aiff]")
{
    TestDirectory t;
    Big body;
    chunk(body, "COMM", commChunk(1, 44100, 16, 44100.0));
    chunk(body, "basc", bascChunk(2, 8, 55, 0));
    auto result = extractAiffMetadata(writeAiff(t, "v2.aif", body));
    auto* bpm = findCandidate(result, SmartField::Bpm, "aiff_basc");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->status == CandidateStatus::Unsupported);
}
TEST_CASE("Truncated AIFF chunk stops safely", "[smart][aiff]")
{
    TestDirectory t;
    Big body;
    chunk(body, "COMM", commChunk(1, 44100, 16, 44100.0));
    body.ascii("basc"); body.u32be(0x7fffffff);
    auto result = extractAiffMetadata(writeAiff(t, "truncated.aif", body));
    REQUIRE(result.error.contains("claims more bytes"));
}
