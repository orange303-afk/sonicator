#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/AbletonAsd.h"
#include "TestHelpers.h"
#include <cstring>
using namespace sonicator;
namespace {
struct Le
{
    std::vector<juce::uint8> v;
    void raw(const void* data, size_t size) { auto* p = (const juce::uint8*) data; v.insert(v.end(), p, p + size); }
    void ascii(const juce::String& s) { raw(s.toRawUTF8(), s.getNumBytesAsUTF8()); }
    void u8(juce::uint8 x) { v.push_back(x); }
    void u16(juce::uint16 x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
    void u32(juce::uint32 x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); }
    void f64(double x) { juce::uint64 bits; std::memcpy(&bits, &x, sizeof(bits)); for (int i = 0; i < 8; ++i) v.push_back((bits >> (8 * i)) & 0xff); }
    void utf16(const juce::String& s) { for (auto c : s) { v.push_back(c & 0xff); v.push_back(c >> 8); } }
};
void addMarker(Le& out, int index, double seconds, double beats)
{
    out.ascii("WarpMarker"); out.u32((juce::uint32) index); out.f64(seconds); out.f64(beats);
}
void addAuf(Le& out, double tempo, bool isSet)
{
    out.raw("\x00\x0b", 2); out.ascii("AufTaktData"); out.u32(4);
    auto member = [&](const juce::String& name, juce::uint8 tag)
    {
        out.u32((juce::uint32) name.length()); out.utf16(name); out.u8(tag);
    };
    member("PreprocessedDataChunk", 0x31);
    member("UnbiasedTempoEstimate", 0x17);
    member("IsSet", 0x10);
    member("Version", 0x11);
    out.u32(0);        // empty blob
    out.f64(tempo);
    out.u8(isSet ? 1 : 0);
    out.u32(5);
}
juce::File writeAsd(TestDirectory& t, const juce::String& name, Le body, int version = 6)
{
    Le file;
    file.u8((juce::uint8) version); file.u8('I'); file.u16(3);
    file.u32(0); file.u32(1); file.u16(0); file.u32(12345);
    file.ascii("SampleData");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name); path.deleteFile();
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
const SmartCandidate* find(const AsdMetadataResult& result, const juce::String& source)
{
    for (const auto& c : result.candidates) if (c.source == source) return &c;
    return nullptr;
}
}
TEST_CASE("ASD warp markers produce an implied tempo and frame count", "[smart][asd]")
{
    TestDirectory t;
    Le body;
    addMarker(body, 0, 0.0, 0.0);
    addMarker(body, 1, 2.0, 4.0);
    auto result = extractAsdMetadata(writeAsd(t, "loop.wav.asd", body));
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.version == 6);
    REQUIRE(result.frameCount == 12345);
    REQUIRE(result.warpMarkerCount == 2);
    auto* bpm = find(result, "asd_warp");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->numericValue == Catch::Approx(120.0));
    REQUIRE(bpm->status == CandidateStatus::Valid);
}
TEST_CASE("Variable warp tempo is not reduced to one value", "[smart][asd]")
{
    TestDirectory t;
    Le body;
    addMarker(body, 0, 0.0, 0.0);
    addMarker(body, 1, 1.0, 2.0);  // 120
    addMarker(body, 2, 2.0, 5.0);  // 180
    auto result = extractAsdMetadata(writeAsd(t, "variable.wav.asd", body));
    auto* bpm = find(result, "asd_warp");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->status == CandidateStatus::Invalid);
    REQUIRE(bpm->detail.contains("variable"));
}
TEST_CASE("AufTaktData with IsSet but zero tempo is invalid provenance", "[smart][asd]")
{
    TestDirectory t;
    Le body;
    addAuf(body, 0.0, true);
    auto result = extractAsdMetadata(writeAsd(t, "zero.wav.asd", body));
    auto* tempo = find(result, "asd_tempo");
    REQUIRE(tempo != nullptr);
    REQUIRE(tempo->status == CandidateStatus::Invalid);
    REQUIRE(tempo->detail.contains("IsSet"));
}
TEST_CASE("A plausible AufTaktData estimate is kept", "[smart][asd]")
{
    TestDirectory t;
    Le body;
    addAuf(body, 128.0, true);
    auto result = extractAsdMetadata(writeAsd(t, "tempo.wav.asd", body));
    auto* tempo = find(result, "asd_tempo");
    REQUIRE(tempo != nullptr);
    REQUIRE(tempo->numericValue == Catch::Approx(128.0));
    REQUIRE(tempo->status == CandidateStatus::Valid);
}
TEST_CASE("Unknown ASD version is reported, not parsed as a guess", "[smart][asd]")
{
    TestDirectory t;
    Le body;
    addMarker(body, 0, 0.0, 0.0);
    addMarker(body, 1, 2.0, 4.0);
    auto result = extractAsdMetadata(writeAsd(t, "v7.wav.asd", body, 7));
    REQUIRE(result.supported == false);
    auto* bpm = find(result, "asd_warp");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->status == CandidateStatus::Unsupported);
}
TEST_CASE("Truncated ASD is rejected without reading out of bounds", "[smart][asd]")
{
    TestDirectory t;
    auto path = t.dir.getChildFile("tiny.asd");
    const juce::uint8 bytes[] = { 6, 'I', 3, 0 };
    path.replaceWithData(bytes, sizeof(bytes));
    auto result = extractAsdMetadata(path);
    REQUIRE(result.error.isNotEmpty());
    REQUIRE(result.candidates.empty());
}
