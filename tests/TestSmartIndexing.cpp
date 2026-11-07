#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/SmartAnalysisService.h"
#include "core/smart/SmartResolver.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace {
struct Bytes
{
    std::vector<juce::uint8> v;
    void ascii(const juce::String& s) { v.insert(v.end(), s.toRawUTF8(), s.toRawUTF8() + s.getNumBytesAsUTF8()); }
    void u16le(juce::uint16 x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
    void u32le(juce::uint32 x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); }
    void f32le(float x) { juce::uint32 b; std::memcpy(&b, &x, 4); u32le(b); }
    void f64(double x) { juce::uint64 b; std::memcpy(&b, &x, 8); for (int i = 0; i < 8; ++i) v.push_back((b >> (8 * i)) & 0xff); }
};
void chunk(Bytes& out, const char* id, Bytes payload)
{
    out.ascii(id); out.u32le((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
juce::File writeAcidWav(TestDirectory& t, const juce::String& name, float tempo)
{
    Bytes fmt; fmt.u16le(1); fmt.u16le(1); fmt.u32le(44100); fmt.u32le(88200); fmt.u16le(2); fmt.u16le(16);
    Bytes data; data.v.resize(4000, 0);
    Bytes acid; acid.u32le(0); acid.u16le(60); acid.u16le(0); acid.u32le(0);
    acid.u32le(8); acid.u16le(4); acid.u16le(4); acid.f32le(tempo);
    Bytes body; chunk(body, "fmt ", fmt); chunk(body, "data", data); chunk(body, "acid", acid);
    Bytes file; file.ascii("RIFF"); file.u32le((juce::uint32) body.v.size() + 4); file.ascii("WAVE");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
juce::File writeAsd(TestDirectory& t, const juce::String& name, double seconds)
{
    Bytes b;
    auto marker = [&](int index, double s, double beats)
    {
        b.ascii("WarpMarker"); b.u32le((juce::uint32) index); b.f64(s); b.f64(beats);
    };
    marker(0, 0.0, 0.0);
    marker(1, seconds, seconds * 2.0); // 120 BPM
    Bytes file;
    file.v.push_back(6); file.v.push_back('I'); file.u16le(3); file.u32le(0); file.u32le(1); file.u16le(0);
    file.u32le(12345); file.ascii("SampleData");
    file.v.insert(file.v.end(), b.v.begin(), b.v.end());
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
SampleRecord recordFor(const juce::File& file)
{
    SampleRecord r;
    r.filePath = file.getFullPathName(); r.fileName = file.getFileName();
    r.fileSize = file.getSize(); r.dateModified = file.getLastModificationTime().toMilliseconds();
    r.fileHash = "h"; r.version = basicFeaturesVersion;
    return r;
}
}
TEST_CASE("The service collects name, WAV and ASD candidates for one file", "[smart][service]")
{
    TestDirectory t;
    auto wav = writeAcidWav(t, "kick_128bpm.wav", 120.0f);
    auto asd = writeAsd(t, "kick_128bpm.wav.asd", 2.0);
    REQUIRE(asd.existsAsFile());
    auto record = recordFor(wav);
    auto candidates = SmartAnalysisService::collectCandidates(record);
    auto result = resolveSmart(candidates);
    REQUIRE(result.bpmState == SmartState::Conflicting);
    REQUIRE(result.bpm == Catch::Approx(120.0)); // ACID outranks the file name
    REQUIRE(result.explanation.contains("filename=128"));
    REQUIRE(result.explanation.contains("asd_warp=120"));
}
TEST_CASE("The background job stores results and inputs for every sample", "[smart][service]")
{
    TestDirectory t;
    auto wav = writeAcidWav(t, "bass_90bpm.wav", 140.0f);
    Database db(t.dir.getChildFile("smart.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto id = repo.upsert(recordFor(wav));
    REQUIRE(repo.samplesNeedingSmartAnalysis(0, 10).size() == 1);
    SmartAnalysisService service(repo);
    REQUIRE(service.start());
    REQUIRE(service.waitUntilFinished(10000));
    const auto progress = service.progress();
    REQUIRE(progress.processed == 1);
    REQUIRE(progress.failed == 0);
    auto result = repo.smartResult(id);
    REQUIRE(result.has_value());
    REQUIRE(result->hasBpm);
    REQUIRE(result->bpm == Catch::Approx(140.0));
    REQUIRE(result->bpmState == SmartState::Conflicting);
    REQUIRE(repo.samplesNeedingSmartAnalysis(0, 10).empty());
    auto inputs = repo.smartInputs(id);
    REQUIRE(inputs.size() == 1);
    REQUIRE(inputs.front().kind == "audio");
}
TEST_CASE("A changed sidecar is detected and refreshSample rewrites the result", "[smart][service]")
{
    TestDirectory t;
    auto wav = writeAcidWav(t, "loop.wav", 120.0f);
    auto asd = writeAsd(t, "loop.wav.asd", 2.0);
    Database db(t.dir.getChildFile("smart-refresh.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto id = repo.upsert(recordFor(wav));
    SmartAnalysisService service(repo);
    REQUIRE(service.refreshSample(id));
    auto stored = repo.smartInputs(id);
    REQUIRE(stored.size() == 2);
    auto current = SmartAnalysisService::currentInputs(*repo.findById(id));
    REQUIRE_FALSE(SmartAnalysisService::inputsDiffer(stored, current));
    // Append bytes: the sidecar changed even though the audio did not.
    asd.appendData("\0\0\0\0", 4);
    auto changed = SmartAnalysisService::currentInputs(*repo.findById(id));
    REQUIRE(SmartAnalysisService::inputsDiffer(stored, changed));
    REQUIRE(service.refreshSample(id));
    REQUIRE_FALSE(SmartAnalysisService::inputsDiffer(repo.smartInputs(id),
                                                     SmartAnalysisService::currentInputs(*repo.findById(id))));
}
TEST_CASE("Reanalysed audio invalidates the stored smart result but keeps overrides", "[smart][service]")
{
    TestDirectory t;
    auto wav = writeAcidWav(t, "snare.wav", 100.0f);
    Database db(t.dir.getChildFile("smart-invalidate.sqlite"));
    SampleRepository repo(db); repo.createTables();
    auto record = recordFor(wav);
    const auto id = repo.upsert(record);
    SmartAnalysisService service(repo);
    REQUIRE(service.refreshSample(id));
    REQUIRE(repo.smartResult(id).has_value());
    SmartOverride overrideValue; overrideValue.numericValue = 90.0;
    repo.setSmartOverride(id, SmartField::Bpm, overrideValue);
    record.duration = 1.0;
    repo.upsert(record);
    // The previous completed generation is retained but marked stale; it is never presented as
    // current, and a manual override is untouched by the invalidation.
    auto stale = repo.smartResult(id);
    REQUIRE(stale.has_value());
    REQUIRE(stale->pipelineState == SmartPipelineState::Stale);
    REQUIRE(stale->bpm == Catch::Approx(90.0)); // the manual override is still effective
    REQUIRE(repo.smartOverrides(id).count(SmartField::Bpm) == 1);
}
