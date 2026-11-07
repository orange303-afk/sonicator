#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "core/database/SearchSyntax.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace {
int64_t addSample(SampleRepository& repo, const juce::String& name, const juce::String& format)
{
    SampleRecord r;
    r.filePath = "/lib/" + name; r.fileName = name; r.format = format; r.fileHash = name;
    return repo.upsert(r);
}
int count(SampleRepository& repo, const juce::String& text)
{
    LibraryFilter filter; filter.text = text;
    return repo.query(filter, 100, 0).total;
}
}
TEST_CASE("Search terms understand masks, formats, types and notes", "[search][syntax]")
{
    auto terms = parseSearchTerms("*.rx2 ext:wav type:pad pad key:Am Amin note:C4 C4 plain");
    REQUIRE(terms.size() == 9);
    REQUIRE(terms[0].kind == SearchKind::Glob);
    REQUIRE(terms[1].kind == SearchKind::Format);
    REQUIRE(terms[1].text == "wav");
    REQUIRE(terms[2].kind == SearchKind::TypeOnly);
    REQUIRE(terms[3].kind == SearchKind::TypeOrText);
    REQUIRE(terms[4].kind == SearchKind::KeyOnly);
    REQUIRE(terms[4].tonic == 9);
    REQUIRE(terms[4].mode == "minor");
    REQUIRE(terms[5].kind == SearchKind::KeyOrText);
    REQUIRE(terms[6].kind == SearchKind::NoteOnly);
    REQUIRE(terms[6].midi == 60);
    REQUIRE(terms[7].kind == SearchKind::KeyOrText);
    REQUIRE(terms[8].kind == SearchKind::Text);
    REQUIRE(globToLike("kick_*.rx2") == "kick\\_%.rx2");
}
TEST_CASE("Masks and formats filter the whole catalog", "[search][database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("search.sqlite"));
    SampleRepository repo(db); repo.createTables();
    addSample(repo, "a.rx2", "RX2");
    addSample(repo, "b.wav", "WAV");
    addSample(repo, "kick_01.wav", "WAV");
    addSample(repo, "kickX01.wav", "WAV");
    REQUIRE(count(repo, "*.rx2") == 1);
    REQUIRE(count(repo, "kick_*") == 1);
    REQUIRE(count(repo, "kick?01*") == 2); // masks apply to the complete file name
    REQUIRE(count(repo, "kick?01") == 0);
    REQUIRE(count(repo, "ext:wav") == 3);
    REQUIRE(count(repo, "format:rx2") == 1);
    REQUIRE(count(repo, "*.rex") == 0);
}
TEST_CASE("Type shorthand uses smart results and falls back to names", "[search][database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("search-type.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto mystery = addSample(repo, "mystery.wav", "WAV");
    addSample(repo, "Pad C4.wav", "WAV");
    addSample(repo, "kick.wav", "WAV");
    SmartResult result; result.type = "pad"; result.typeState = SmartState::Resolved;
    repo.setSmartResult(mystery, result);
    REQUIRE(count(repo, "type:pad") == 1);
    REQUIRE(count(repo, "pad") == 2); // smart result plus the file name
}
TEST_CASE("Key and note shorthand read smart results", "[search][database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("search-key.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto chord = addSample(repo, "chord.wav", "WAV");
    const auto bass = addSample(repo, "bass.wav", "WAV");
    const auto pad = addSample(repo, "mystery.pad", "WAV");
    const auto silent = addSample(repo, "silent.wav", "WAV");
    SmartResult chordResult; chordResult.keyTonic = 0; chordResult.keyMode = "minor";
    chordResult.keyState = SmartState::Resolved; repo.setSmartResult(chord, chordResult);
    SmartResult bassResult; bassResult.keyTonic = 9; bassResult.keyMode = "minor";
    bassResult.keyState = SmartState::Resolved; repo.setSmartResult(bass, bassResult);
    SmartResult padResult; padResult.rootNote = 60; padResult.rootState = SmartState::Resolved;
    repo.setSmartResult(pad, padResult);
    SmartResult silentResult; silentResult.rootNote = 60; silentResult.rootState = SmartState::Default;
    repo.setSmartResult(silent, silentResult);
    REQUIRE(count(repo, "key:Am") == 1);
    REQUIRE(count(repo, "key:Cmin") == 1);
    REQUIRE(count(repo, "Amin") == 1);       // smart key, not the file name
    REQUIRE(count(repo, "note:C4") == 1);    // default sampler roots are excluded
}
TEST_CASE("Literal percent and underscore never become SQL wildcards", "[search][database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("search-literal.sqlite"));
    SampleRepository repo(db); repo.createTables();
    addSample(repo, "100%_kick.wav", "WAV");
    addSample(repo, "other.wav", "WAV");
    REQUIRE(count(repo, "100%") == 1);
    REQUIRE(count(repo, "kick_01") == 0);
    REQUIRE(count(repo, "foo:bar") == 0);
}
TEST_CASE("Manual overrides are the effective type and key in search", "[search][database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("search-override.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto sample = addSample(repo, "mystery.wav", "WAV");
    SmartOverride type; type.textValue = "pad";
    repo.setSmartOverride(sample, SmartField::Type, type);
    SmartOverride key; key.textValue = "D minor";
    repo.setSmartOverride(sample, SmartField::Key, key);
    REQUIRE(count(repo, "type:pad") == 1);
    REQUIRE(count(repo, "key:Dm") == 1);
    REQUIRE(count(repo, "type:kick") == 0);
    SmartOverride unknownType; unknownType.cleared = true;
    repo.setSmartOverride(sample, SmartField::Type, unknownType);
    REQUIRE(count(repo, "type:pad") == 0);
}
