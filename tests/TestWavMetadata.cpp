#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/WavMetadata.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace {
struct Bytes
{
    std::vector<juce::uint8> v;
    void raw(const void* data, size_t size)
    {
        const auto* p = (const juce::uint8*) data;
        v.insert(v.end(), p, p + size);
    }
    void ascii(const juce::String& text) { raw(text.toRawUTF8(), text.getNumBytesAsUTF8()); }
    void u16le(juce::uint16 value) { v.push_back(value & 0xff); v.push_back(value >> 8); }
    void u32le(juce::uint32 value)
    {
        for (int i = 0; i < 4; ++i) v.push_back((value >> (8 * i)) & 0xff);
    }
    void u32be(juce::uint32 value)
    {
        for (int i = 3; i >= 0; --i) v.push_back((value >> (8 * i)) & 0xff);
    }
    void f32le(float value)
    {
        juce::uint32 bits = 0; std::memcpy(&bits, &value, sizeof(bits)); u32le(bits);
    }
};
void chunk(Bytes& out, const char* id, Bytes payload, int declaredSize = -1)
{
    out.raw(id, 4);
    out.u32le(declaredSize >= 0 ? (juce::uint32) declaredSize : (juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
Bytes fmtChunk(int channels, int rate, int bits)
{
    Bytes b; b.u16le(1); b.u16le((juce::uint16) channels); b.u32le((juce::uint32) rate);
    b.u32le((juce::uint32) (rate * channels * bits / 8)); b.u16le((juce::uint16) (channels * bits / 8));
    b.u16le((juce::uint16) bits);
    return b;
}
Bytes acidChunk(float tempo, int root, int beats)
{
    Bytes b; b.u32le(0); b.u16le((juce::uint16) root); b.u16le(0); b.u32le(0);
    b.u32le((juce::uint32) beats); b.u16le(4); b.u16le(4); b.f32le(tempo);
    return b;
}
Bytes smplChunk(int unity, int loops)
{
    Bytes b;
    b.u32le(0); b.u32le(0); b.u32le(0);      // manufacturer, product, sample period
    b.u32le((juce::uint32) unity);           // MIDI unity note at offset 12
    b.u32le(0); b.u32le(0); b.u32le(0);      // pitch fraction, SMPTE format/offset
    b.u32le((juce::uint32) loops);           // number of loops at offset 28
    b.u32le(0);                              // sampler data
    return b;
}
Bytes infoChunk(const juce::String& id, const juce::String& text)
{
    Bytes item;
    item.ascii(text);
    item.v.push_back(0);
    Bytes payload; payload.ascii("INFO"); payload.raw(id.toRawUTF8(), 4);
    payload.u32le((juce::uint32) item.v.size());
    payload.v.insert(payload.v.end(), item.v.begin(), item.v.end());
    while (payload.v.size() & 1) payload.v.push_back(0);
    return payload;
}
juce::File writeRiff(TestDirectory& t, const juce::String& name, Bytes body, bool rf64 = false)
{
    Bytes file;
    file.ascii(rf64 ? "RF64" : "RIFF");
    file.u32le((juce::uint32) body.v.size() + 4);
    file.ascii("WAVE");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name);
    path.deleteFile();
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
const SmartCandidate* findCandidate(const WavMetadataResult& result, SmartField field,
                                    const juce::String& source)
{
    for (const auto& c : result.candidates)
        if (c.field == field && c.source == source) return &c;
    return nullptr;
}
}
TEST_CASE("WAV metadata reads chunks after data and never treats sampler root as key", "[smart][wav]")
{
    TestDirectory t;
    Bytes body;
    chunk(body, "fmt ", fmtChunk(2, 44100, 16));
    chunk(body, "data", Bytes { std::vector<juce::uint8>(44100 * 4, 0) });
    chunk(body, "acid", acidChunk(140.0f, 60, 8));
    chunk(body, "smpl", smplChunk(60, 1));
    chunk(body, "LIST", infoChunk("IKEY", "Am"));
    auto file = writeRiff(t, "meta.wav", body);
    auto result = extractWavMetadata(file);
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.info.channels == 2);
    REQUIRE(result.info.frames == 44100);
    auto* bpm = findCandidate(result, SmartField::Bpm, "wav_acid");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->numericValue == Catch::Approx(140.0));
    REQUIRE(bpm->status == CandidateStatus::Valid);
    auto* key = findCandidate(result, SmartField::Key, "info");
    REQUIRE(key != nullptr);
    REQUIRE(key->textValue == "A minor");
    auto* root = findCandidate(result, SmartField::RootNote, "wav_smpl");
    REQUIRE(root != nullptr);
    REQUIRE(root->numericValue == 60);
    REQUIRE(root->status == CandidateStatus::DefaultValue);
    REQUIRE(findCandidate(result, SmartField::Key, "wav_smpl") == nullptr);
}
TEST_CASE("Odd-sized chunks are padded correctly and later chunks still parse", "[smart][wav]")
{
    TestDirectory t;
    Bytes odd; odd.ascii("abc"); // size 3, one pad byte
    Bytes body;
    chunk(body, "fmt ", fmtChunk(1, 48000, 16));
    chunk(body, "JUNK", odd);
    chunk(body, "LIST", infoChunk("IKEY", "C#min"));
    auto result = extractWavMetadata(writeRiff(t, "odd.wav", body));
    REQUIRE(result.error.isEmpty());
    auto* key = findCandidate(result, SmartField::Key, "info");
    REQUIRE(key != nullptr);
    REQUIRE(key->textValue == "C# minor");
}
TEST_CASE("A chunk size beyond EOF stops parsing but keeps earlier candidates", "[smart][wav]")
{
    TestDirectory t;
    Bytes body;
    chunk(body, "fmt ", fmtChunk(1, 44100, 16));
    chunk(body, "acid", acidChunk(120.0f, 60, 4));
    body.ascii("data"); body.u32le(0x7fffffff); // lies about its size
    auto result = extractWavMetadata(writeRiff(t, "truncated.wav", body));
    REQUIRE(result.error.contains("claims more bytes"));
    auto* bpm = findCandidate(result, SmartField::Bpm, "wav_acid");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->numericValue == Catch::Approx(120.0));
}
TEST_CASE("IKEY keywords are kept as invalid provenance, not as a key", "[smart][wav]")
{
    TestDirectory t;
    Bytes body;
    chunk(body, "fmt ", fmtChunk(1, 44100, 16));
    chunk(body, "LIST", infoChunk("IKEY", "Cataract"));
    auto result = extractWavMetadata(writeRiff(t, "keywords.wav", body));
    auto* key = findCandidate(result, SmartField::Key, "info");
    REQUIRE(key != nullptr);
    REQUIRE(key->status == CandidateStatus::Invalid);
    REQUIRE(key->textValue.isEmpty());
}
TEST_CASE("RF64 is reported as unsupported", "[smart][wav]")
{
    TestDirectory t;
    Bytes body; chunk(body, "fmt ", fmtChunk(1, 44100, 16));
    auto result = extractWavMetadata(writeRiff(t, "rf64.wav", body, true));
    REQUIRE(result.info.rf64);
    REQUIRE(result.error.contains("RF64"));
}
TEST_CASE("ID3 TBPM and TKEY are read from a WAV chunk", "[smart][wav]")
{
    TestDirectory t;
    Bytes body;
    chunk(body, "fmt ", fmtChunk(1, 44100, 16));
    Bytes frameTbpm; frameTbpm.v.push_back(0); frameTbpm.ascii("128");
    Bytes frameTkey; frameTkey.v.push_back(0); frameTkey.ascii("F#m");
    Bytes id3; id3.ascii("ID3"); id3.v.push_back(3); id3.v.push_back(0); id3.v.push_back(0);
    const juce::uint32 framesSize = 10 + (int) frameTbpm.v.size() + 10 + (int) frameTkey.v.size();
    id3.v.push_back(0); id3.v.push_back(0); id3.v.push_back(0); id3.v.push_back((juce::uint8) framesSize);
    auto addFrame = [&](const char* id, Bytes payload)
    {
        id3.ascii(id); id3.u32be((juce::uint32) payload.v.size());
        id3.v.push_back(0); id3.v.push_back(0);
        id3.v.insert(id3.v.end(), payload.v.begin(), payload.v.end());
    };
    addFrame("TBPM", frameTbpm);
    addFrame("TKEY", frameTkey);
    chunk(body, "id3 ", id3);
    auto result = extractWavMetadata(writeRiff(t, "id3.wav", body));
    auto* bpm = findCandidate(result, SmartField::Bpm, "id3");
    REQUIRE(bpm != nullptr);
    REQUIRE(bpm->numericValue == Catch::Approx(128.0));
    auto* key = findCandidate(result, SmartField::Key, "id3");
    REQUIRE(key != nullptr);
    REQUIRE(key->textValue == "F# minor");
}
