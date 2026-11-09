#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/SmartAnalysisService.h"
#include "core/smart/SmartResolver.h"
#include "core/smart/RhythmCandidates.h"
#include "TestHelpers.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
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
TEST_CASE("Folder smart analysis covers the subtree only and keeps measured features", "[smart][folders]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("lib/Drums");
    const auto fileA = t.wave("lib/Drums/kick 120bpm.wav");
    const auto fileB = t.wave("lib/Drums/deep/snare.wav");
    const auto outside = t.wave("lib/Other/pad.wav");
    Database db(t.dir.getChildFile("smart-folder.sqlite")); SampleRepository repo(db); repo.createTables();
    auto add = [&](const juce::File& f)
    {
        SampleRecord r; r.filePath = f.getFullPathName(); r.fileName = f.getFileName();
        r.fileHash = "measured-" + f.getFileName(); r.duration = 1.25;
        r.version = basicFeaturesVersion;
        return repo.upsert(r);
    };
    const auto idA = add(fileA), idB = add(fileB), idOutside = add(outside);
    const auto keep = repo.createTag("Keep", 0xff445566);
    REQUIRE(repo.assignTag({ idA }, keep) == 1);
    repo.setStarred({ idA }, true);
    SmartAnalysisService smart(repo);
    REQUIRE(smart.startFolder(folder.getFullPathName()));
    REQUIRE(smart.waitUntilFinished(15000));
    const auto p = smart.progress();
    REQUIRE(p.processed == 2);
    REQUIRE(p.total == 2);
    REQUIRE(repo.smartResult(idA).has_value());
    REQUIRE(repo.smartResult(idB).has_value());
    REQUIRE_FALSE(repo.smartResult(idOutside).has_value());
    REQUIRE(repo.smartResult(idA)->bpm == Catch::Approx(120.0));
    // Smart analysis never rewrites measured features or the content hash.
    const auto stored = repo.findById(idA);
    REQUIRE(stored->duration == Catch::Approx(1.25));
    REQUIRE(stored->fileHash == "measured-kick 120bpm.wav");
    REQUIRE(stored->version == basicFeaturesVersion);
    REQUIRE(stored->starred);
    REQUIRE(repo.tagsForSample(idA).size() == 1);
}
TEST_CASE("Folder analysis of an empty subtree completes with no work", "[smart][folders][empty]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("smart-empty.sqlite")); SampleRepository repo(db); repo.createTables();
    SmartAnalysisService smart(repo);
    REQUIRE(smart.startFolder(t.dir.getChildFile("lib/none").getFullPathName()));
    REQUIRE(smart.waitUntilFinished(10000));
    REQUIRE(smart.progress().processed == 0);
    REQUIRE(smart.progress().stage == SmartStage::Complete);
}

namespace {
juce::File writeClickLoop(TestDirectory& t, const juce::String& name, double bpm, double seconds)
{
    juce::AudioBuffer<float> audio(1, (int) std::llround(seconds * 44100.0));
    audio.clear();
    const double interval = 60.0 / bpm;
    for (double time = 0.05; time < seconds; time += interval)
    {
        const int start = (int) std::llround(time * 44100.0);
        for (int i = 0; i < 1058 && start + i < audio.getNumSamples(); ++i)
        {
            const double x = (double) i / 44100.0;
            audio.addSample(0, start + i,
                            (float) (0.8 * std::exp(-x / 0.03)
                                     * std::sin(2.0 * juce::MathConstants<double>::pi * 1200.0 * x)));
        }
    }
    const auto file = t.dir.getChildFile(name);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr) return file;
    auto* writer = wav.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0);
    if (writer == nullptr) return file;
    stream.release();
    writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    delete writer;
    return file;
}
} // namespace

TEST_CASE("A rhythmic WAV gets DSP structure and tempo through the smart pipeline",
          "[smart][service][dsp]")
{
    TestDirectory t;
    auto wav = writeClickLoop(t, "drum_pattern.wav", 120.0, 4.0);
    REQUIRE(wav.getSize() > 0);
    auto candidates = SmartAnalysisService::collectCandidates(recordFor(wav));
    auto result = resolveSmart(candidates);
    REQUIRE(result.structureSource == "dsp");
    REQUIRE(result.structure == "loop");
    REQUIRE(result.structureState == SmartState::Resolved);
    REQUIRE(result.hasBpm);
    REQUIRE(result.bpm == Catch::Approx(120.0).epsilon(0.02));
    REQUIRE(result.bpmSource == "dsp");
    REQUIRE(result.bpmState == SmartState::Resolved);
    REQUIRE(result.explanation.contains("confidence"));
}
TEST_CASE("A one-shot WAV gets a structure but no DSP tempo", "[smart][service][dsp]")
{
    TestDirectory t;
    auto wav = t.dir.getChildFile("kick_hit.wav");
    juce::AudioBuffer<float> audio(1, 2 * 44100);
    audio.clear();
    for (int i = 0; i < 2205; ++i)
    {
        const double x = (double) i / 44100.0;
        audio.setSample(0, 4410 + i,
                        (float) (0.8 * std::exp(-x / 0.03)
                                 * std::sin(2.0 * juce::MathConstants<double>::pi * 900.0 * x)));
    }
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> out(wav.createOutputStream());
    REQUIRE(out != nullptr);
    auto* writer = wavFormat.createWriterFor(out.get(), 44100.0, 1, 16, {}, 0);
    REQUIRE(writer != nullptr);
    out.release();
    REQUIRE(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()));
    delete writer;
    auto result = resolveSmart(SmartAnalysisService::collectCandidates(recordFor(wav)));
    REQUIRE(result.structureSource == "dsp");
    REQUIRE(result.structure == "one-shot");
    REQUIRE_FALSE(result.hasBpm);
    REQUIRE(result.bpmState == SmartState::Unknown);
}

namespace {
// A WAV that carries both a real click pattern and ACID tempo metadata, used to prove that the
// metadata answer suppresses the expensive DSP pass.
juce::File writeAcidClickWav(TestDirectory& t, const juce::String& name, float tempo,
                             double clickBpm, double seconds)
{
    juce::AudioBuffer<float> audio(1, (int) std::llround(seconds * 44100.0));
    audio.clear();
    const double interval = 60.0 / clickBpm;
    for (double time = 0.05; time < seconds; time += interval)
    {
        const int start = (int) std::llround(time * 44100.0);
        for (int i = 0; i < 1058 && start + i < audio.getNumSamples(); ++i)
        {
            const double x = (double) i / 44100.0;
            audio.addSample(0, start + i,
                            (float) (0.8 * std::exp(-x / 0.03)
                                     * std::sin(2.0 * juce::MathConstants<double>::pi * 1200.0 * x)));
        }
    }
    Bytes data;
    data.v.reserve((size_t) audio.getNumSamples() * 2);
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const int value = (int) std::lround(
            juce::jlimit(-1.0f, 1.0f, audio.getSample(0, i)) * 32767.0f);
        data.u16le((juce::uint16) value);
    }
    Bytes fmt; fmt.u16le(1); fmt.u16le(1); fmt.u32le(44100); fmt.u32le(88200);
    fmt.u16le(2); fmt.u16le(16);
    Bytes acid; acid.u32le(0); acid.u16le(60); acid.u16le(0); acid.u32le(0);
    acid.u32le(8); acid.u16le(4); acid.u16le(4); acid.f32le(tempo);
    Bytes body; chunk(body, "fmt ", fmt); chunk(body, "data", data); chunk(body, "acid", acid);
    Bytes file; file.ascii("RIFF"); file.u32le((juce::uint32) body.v.size() + 4); file.ascii("WAVE");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
} // namespace

TEST_CASE("Rhythm analysis is requested only without a usable metadata tempo", "[smart][dsp]")
{
    REQUIRE(needsRhythmAnalysis({}));
    SmartCandidate bpm;
    bpm.field = SmartField::Bpm;
    bpm.source = "wav_acid";
    bpm.numericValue = 120.0;
    bpm.status = CandidateStatus::Valid;
    REQUIRE_FALSE(needsRhythmAnalysis({ bpm }));
    bpm.status = CandidateStatus::Invalid;
    REQUIRE(needsRhythmAnalysis({ bpm }));
    bpm.status = CandidateStatus::Valid;
    bpm.numericValue = 900.0;
    REQUIRE(needsRhythmAnalysis({ bpm }));
}
TEST_CASE("A metadata tempo suppresses the DSP pass", "[smart][service][dsp]")
{
    TestDirectory t;
    auto wav = writeAcidClickWav(t, "drum_groove_metadata.wav", 120.0f, 120.0, 4.0);
    auto candidates = SmartAnalysisService::collectCandidates(recordFor(wav));
    REQUIRE(std::none_of(candidates.begin(), candidates.end(), [](const SmartCandidate& c)
    {
        return c.source == "dsp";
    }));
    auto result = resolveSmart(candidates);
    REQUIRE(result.bpm == Catch::Approx(120.0).epsilon(0.01));
    REQUIRE(result.bpmSource == "wav_acid");
    REQUIRE(result.structure.isEmpty());
}
