#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/indexing/IndexingService.h"
#include "core/database/SampleRepository.h"
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/database/SearchSyntax.h"
#include "TestHelpers.h"
using namespace sonicator;
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
juce::File writeRex(TestDirectory& t, const juce::String& name)
{
    Be glob; glob.u32be(2); glob.u16be(1); glob.v.push_back(0); glob.v.push_back(4); glob.v.push_back(4);
    glob.v.push_back(0x4e); glob.u16be(0); glob.u16be(1200); glob.u16be(1); glob.u32be(120000);
    glob.v.push_back(1); glob.v.push_back(0);
    Be sinf; sinf.v.push_back(2); sinf.v.push_back(3); sinf.u32be(44100); sinf.u32be(88200);
    sinf.u32be(0); sinf.u32be(88200);
    Be slices; slices.ascii("SLCL");
    Be one; one.u32be(0); one.u32be(1000); one.u16be(0x7fff); one.v.push_back(0);
    Be two; two.u32be(1000); two.u32be(1000); two.u16be(0x7fff); two.v.push_back(0);
    chunk(slices, "SLCE", one); chunk(slices, "SLCE", two);
    Be body; chunk(body, "GLOB", glob); chunk(body, "SINF", sinf); chunk(body, "CAT ", slices);
    Be file; file.ascii("CAT "); file.u32be((juce::uint32) body.v.size() + 4); file.ascii("REX2");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
juce::File writeSf2(TestDirectory& t, const juce::String& name)
{
    Be file; file.ascii("RIFF"); file.u32be(4); file.ascii("sfbk");
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
int count(SampleRepository& repo, const juce::String& text)
{
    LibraryFilter filter; filter.text = text;
    return repo.query(filter, 100, 0).total;
}
}
TEST_CASE("Metadata-only containers become catalog rows", "[assets][indexing]")
{
    TestDirectory t;
    auto wav = t.wave("tone.wav");
    auto rx2 = writeRex(t, "loop.rx2");
    auto sf2 = writeSf2(t, "drums.sf2");
    REQUIRE(FileScanner::isSupportedFormat(rx2));
    REQUIRE(FileScanner::isMetadataOnlyFormat(sf2));
    auto rx = analyzeMetadataAsset(rx2);
    REQUIRE(rx.error.isEmpty());
    REQUIRE(rx.format == "RX2");
    REQUIRE(rx.version == metadataOnlyVersion);
    REQUIRE(rx.sampleRate == 44100);
    REQUIRE(rx.numChannels == 2);
    REQUIRE(rx.duration == Catch::Approx(2.0));
    auto sf = analyzeMetadataAsset(sf2);
    REQUIRE(sf.error.isEmpty());
    REQUIRE(sf.format == "SF2");
    REQUIRE(sf.version == metadataOnlyVersion);
    Database db(t.dir.getChildFile("assets.sqlite"));
    SampleRepository repo(db); repo.createTables();
    IndexingService indexer(repo);
    REQUIRE(indexer.start({ t.dir.getFullPathName() }));
    REQUIRE(indexer.waitUntilFinished(20000));
    REQUIRE(repo.count() == 3);
    auto wavRecord = repo.findByPath(wav.getFullPathName());
    REQUIRE(wavRecord.has_value());
    REQUIRE(wavRecord->version == basicFeaturesVersion);
    auto rxRecord = repo.findByPath(rx2.getFullPathName());
    REQUIRE(rxRecord.has_value());
    REQUIRE(rxRecord->version == metadataOnlyVersion);
    REQUIRE(rxRecord->error.isEmpty());
    REQUIRE(rxRecord->duration == Catch::Approx(2.0));
    auto sfRecord = repo.findByPath(sf2.getFullPathName());
    REQUIRE(sfRecord.has_value());
    REQUIRE(sfRecord->version == metadataOnlyVersion);
    LibraryFilter visible; visible.hideEmpty = true;
    REQUIRE(repo.query(visible, 100, 0).total == 3); // a zero peak must not hide RX2/SF2 rows
}
TEST_CASE("A second scan skips unchanged metadata-only assets", "[assets][indexing]")
{
    TestDirectory t;
    writeRex(t, "loop.rx2");
    Database db(t.dir.getChildFile("assets-rescan.sqlite"));
    SampleRepository repo(db); repo.createTables();
    {
        IndexingService indexer(repo);
        REQUIRE(indexer.start({ t.dir.getFullPathName() }));
        REQUIRE(indexer.waitUntilFinished(20000));
    }
    IndexingService again(repo);
    REQUIRE(again.start({ t.dir.getFullPathName() }));
    REQUIRE(again.waitUntilFinished(20000));
    REQUIRE(again.progress().skipped == 1);
    REQUIRE(again.progress().analyzed == 0);
}
TEST_CASE("Search can list several formats at once", "[search][assets]")
{
    TestDirectory t; Database db(t.dir.getChildFile("search-formats.sqlite"));
    SampleRepository repo(db); repo.createTables();
    auto add = [&](const juce::String& name, const juce::String& format)
    {
        SampleRecord r; r.filePath = "/lib/" + name; r.fileName = name; r.format = format;
        r.fileHash = name; repo.upsert(r);
    };
    add("a.rx2", "RX2"); add("b.sf2", "SF2"); add("c.wav", "WAV"); add("d.rex", "REX");
    REQUIRE(count(repo, "ext:rx2,sf2") == 2);
    REQUIRE(count(repo, "*.rex/rx2/sf2") == 3); // .rex row exists only if the file exists
    REQUIRE(count(repo, "*.rx2|*.sf2") == 2);
    REQUIRE(count(repo, "*.rx2") == 1);
}
TEST_CASE("The compact format list parses into one format term", "[search][syntax]")
{
    auto terms = parseSearchTerms("*.rex/rx2/sf2");
    REQUIRE(terms.size() == 1);
    REQUIRE(terms[0].kind == SearchKind::Format);
    REQUIRE(terms[0].values.size() == 3);
    REQUIRE(terms[0].values[0] == "rex");
    auto any = parseSearchTerms("*.rx2|*.sf2");
    REQUIRE(any.size() == 1);
    REQUIRE(any[0].kind == SearchKind::GlobOr);
    REQUIRE(any[0].values.size() == 2);
}
