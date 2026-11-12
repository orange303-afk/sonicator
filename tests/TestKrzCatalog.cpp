#include <catch2/catch_test_macros.hpp>
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/indexing/IndexingService.h"
#include "TestHelpers.h"
#include "KrzTestFile.h"
using namespace sonicator;
using namespace krztest;

TEST_CASE("KRZ banks are metadata-only catalog rows", "[krz][catalog]")
{
    TestDirectory dir;
    BankSpec spec;
    KeymapSpec keymap; keymap.id = 200;
    spec.keymaps = { keymap };
    auto file = writeBank(dir.dir, "bank.krz", spec);
    CHECK(FileScanner::isMetadataOnlyFormat(file));
    CHECK(FileScanner::isSupportedFormat(file));
    const auto record = analyzeMetadataAsset(file, {});
    CHECK(record.format == "KRZ");
    CHECK(record.error.isEmpty());
    CHECK(record.compatibility == AssetCompatibility::supported);
    CHECK(record.fileHash.isNotEmpty());
}

TEST_CASE("Continuation parts are hidden from the catalog", "[krz][catalog]")
{
    TestDirectory dir;
    BankSpec spec;
    KeymapSpec keymap; keymap.id = 200;
    spec.keymaps = { keymap };
    auto first = writeBank(dir.dir, "set.kr1.krz", spec);
    auto second = writeBytes(dir.dir, "set.kr2.krz", makeContinuation(0, { 1, 2 }));
    auto upper = writeBytes(dir.dir, "Other.KR3.K25", makeContinuation(0, { 1, 2 }));
    CHECK(FileScanner::isSupportedFormat(first));
    CHECK_FALSE(FileScanner::isSupportedFormat(second));
    CHECK_FALSE(FileScanner::isSupportedFormat(upper));
}

TEST_CASE("A renamed file is rejected as a Kurzweil bank", "[krz][catalog]")
{
    TestDirectory dir;
    auto wave = dir.wave("plain.wav", 440, 1, 100);
    auto fake = dir.dir.getChildFile("fake.krz");
    REQUIRE(wave.copyFileTo(fake));
    const auto record = analyzeMetadataAsset(fake, {});
    CHECK(record.format == "KRZ");
    CHECK(record.compatibility == AssetCompatibility::invalid);
    CHECK(record.error == "Not a Kurzweil K2000/K2500/K2600 bank");
}

TEST_CASE("Indexing a folder keeps one row per set", "[krz][catalog]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200;
    HeaderSpec header; header.startWord = 0; header.endWord = 1;
    sample.headers = { header };
    spec.samples = { sample };
    spec.pcm = { 1, 2 };
    writeBank(dir.dir, "x.kr1.krz", spec);
    writeBytes(dir.dir, "x.kr2.krz", makeContinuation(0, { 3, 4 }));
    BankSpec other;
    other.keymaps = { KeymapSpec {} };
    writeBank(dir.dir, "y.krz", other);

    Database db(dir.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    IndexingService index(repo);
    juce::StringArray roots { dir.dir.getFullPathName() };
    REQUIRE(index.start(roots));
    REQUIRE(index.waitUntilFinished(10000));
    CHECK(index.progress().processed == 2);
    CHECK(repo.count() == 2);
}
