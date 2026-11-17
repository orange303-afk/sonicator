#include <catch2/catch_test_macros.hpp>
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "core/database/SearchSyntax.h"
#include "core/database/LibraryQuery.h"
#include "core/dsp/BasicAnalyzer.h"
#include "core/smart/SmartTypes.h"
#include "TestHelpers.h"

using namespace sonicator;

// The manual's search chapter promises that these examples match something in a real catalog.
// This test pins the examples to the same parser and SQL the library uses.
TEST_CASE("Manual search examples parse and match a real catalog", "[manual][search]")
{
    TestDirectory t;
    const juce::File root = t.dir.getChildFile("Library");
    root.createDirectory();

    Database db(t.dir.getChildFile("manual-claims.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(root.getFullPathName());

    struct File { const char* name; const char* folder; };
    const File files[] {
        { "Kick_808_Am_120.wav", "Drums" }, { "Snare_Tight_01.wav", "Drums" },
        { "Pad_Warm_Cmin.wav", "Pads" }, { "Loop_Dub_Chords_120.wav", "Loops" },
        { "Vox_Chop_Ah.wav", "Vocals" }, { "Hat_Closed_16.wav", "Drums" },
        { "Bass_Sub_Cmin_140.wav", "Bass" }, { "Bass Drum 01.wav", "Bass" }
    };
    std::map<juce::String, int64_t> ids;
    int index = 0;
    for (const auto& entry : files)
    {
        const auto file = root.getChildFile(entry.folder).getChildFile(entry.name);
        file.getParentDirectory().createDirectory();
        t.wave(file.getRelativePathFrom(t.dir), 80.0 + index * 5.0, 1, 22050 + index * 13);
        auto record = BasicAnalyzer().analyzeAndHash(file);
        ids[entry.name] = repo.upsert(record);
        ++index;
    }

    const auto kick = ids.at("Kick_808_Am_120.wav");
    SmartOverride type;
    type.textValue = "Kick";
    repo.setSmartOverride(kick, SmartField::Type, type);
    SmartOverride key;
    key.textValue = "Am";
    key.numericValue = 9;
    key.auxText = "minor";
    repo.setSmartOverride(kick, SmartField::Key, key);
    SmartOverride rootNote;
    rootNote.textValue = "C4";
    rootNote.numericValue = 60;
    repo.setSmartOverride(kick, SmartField::RootNote, rootNote);
    SmartOverride genre;
    genre.textValue = "house";
    repo.setSmartOverride(kick, SmartField::Genre, genre);

    struct Example { const char* query; size_t minimum; };
    const Example examples[] {
        { "kick", 1 }, { "ext:wav", 8 }, { "type:kick", 1 },
        { "key:Am", 1 }, { "note:C4", 1 }, { "genre:house", 1 }, { "\"bass drum\"", 1 }
    };
    for (const auto& example : examples)
    {
        LibraryFilter filter;
        filter.text = example.query;
        INFO("example: " << example.query);
        CHECK(repo.queryIds(filter).size() >= example.minimum);
        CHECK(! parseSearchTerms(example.query).empty());
    }
    for (const char* query : { "*.rx2", "*.rx2|*.sf2", "kick_*", "*.rex/rx2/sf2" })
        CHECK(! parseSearchTerms(query).empty());
}
