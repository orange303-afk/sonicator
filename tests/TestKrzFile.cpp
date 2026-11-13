#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/kurzweil/KrzFile.h"
#include "TestHelpers.h"
#include "KrzTestFile.h"
using namespace sonicator::krz;
using namespace krztest;

namespace {
juce::File bankFile(const TestDirectory& d, const juce::String& name, const BankSpec& spec)
{
    return writeBank(d.dir, name, spec);
}
EntrySpec entry(int sampleId, int subSample = 1, int tuning = 0, int volume = 0)
{
    EntrySpec e; e.sampleId = sampleId; e.subSample = subSample;
    e.tuningCents = tuning; e.volumeHalfDb = volume; return e;
}
KeymapSpec fullKeymap(int id, int sampleId, int method = 0x03, int entrySize = 3)
{
    KeymapSpec k; k.id = id; k.method = method; k.entrySize = entrySize;
    for (int key = 12; key < 128; ++key) k.levels[0].push_back(entry(sampleId));
    return k;
}
} // namespace

TEST_CASE("KRZ mono looped sample decodes with root and loop", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200; sample.name = "Guitar E1";
    HeaderSpec header; header.rootKey = 40; header.looping = true;
    header.startWord = 0; header.loopWord = 2; header.endWord = 5; header.period = 40000;
    sample.headers = { header };
    spec.samples = { sample };
    spec.pcm = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 };
    auto file = bankFile(dir, "mono.krz", spec);

    Bank bank; juce::String error;
    const bool read = readBank(file, bank, error);
    INFO("readBank error: " << error);
    REQUIRE(read);
    CHECK(error.isEmpty());
    REQUIRE(bank.samples.size() == 1);
    CHECK(bank.samples[0].name == "Guitar E1");
    REQUIRE(bank.samples[0].headers.size() == 1);
    CHECK(bank.samples[0].headers[0].rootKey == 40);
    CHECK(bank.samples[0].headers[0].sampleRate == 25000);
    CHECK(bank.samples[0].headers[0].looping);

    DecodedSample decoded; juce::String decodeError;
    REQUIRE(decodeHeader(bank, bank.samples[0], 0, decoded, decodeError));
    REQUIRE(decoded.audio != nullptr);
    CHECK(decoded.audio->sampleRate == 25000);
    CHECK(decoded.audio->channels == 1);
    CHECK(decoded.audio->frames == 8);
    CHECK(decoded.audio->left[0] == Catch::Approx(1000.0f / 32768.0f));
    CHECK(decoded.audio->left[7] == Catch::Approx(8000.0f / 32768.0f));
    CHECK(decoded.rootKey == 40);
    CHECK(decoded.looping);
    CHECK(decoded.loopStart == 2);
    CHECK(decoded.loopEnd == 6); // end word 5 inclusive, exclusive for the player
}

TEST_CASE("KRZ stereo sample object decodes both halves", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200; sample.name = "Stereo Pad"; sample.stereo = true;
    HeaderSpec left; left.rootKey = 60; left.startWord = 0; left.endWord = 3;
    HeaderSpec right = left; right.startWord = 4; right.endWord = 7;
    sample.headers = { left, right };
    spec.samples = { sample };
    spec.pcm = { 1, 2, 3, 4, -1, -2, -3, -4 };
    auto file = bankFile(dir, "stereo.krz", spec);

    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    DecodedSample decoded;
    REQUIRE(decodeStereo(bank, bank.samples[0], decoded, error));
    REQUIRE(decoded.audio != nullptr);
    CHECK(decoded.audio->channels == 2);
    CHECK(decoded.audio->frames == 4);
    CHECK(decoded.audio->left[1] == Catch::Approx(2.0f / 32768.0f));
    CHECK(decoded.audio->right[0] == Catch::Approx(-1.0f / 32768.0f));
}

TEST_CASE("KRZ multisample decodes the requested header", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200; sample.name = "Multi";
    HeaderSpec a; a.startWord = 0; a.endWord = 3;
    HeaderSpec b; b.rootKey = 72; b.startWord = 4; b.endWord = 7;
    HeaderSpec c; c.rootKey = 84; c.startWord = 8; c.endWord = 11;
    sample.headers = { a, b, c };
    spec.samples = { sample };
    spec.pcm = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    auto file = bankFile(dir, "multi.krz", spec);

    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    REQUIRE(bank.samples[0].headers.size() == 3);
    DecodedSample decoded;
    REQUIRE(decodeHeader(bank, bank.samples[0], 2, decoded, error));
    CHECK(decoded.audio->frames == 4);
    CHECK(decoded.audio->left[0] == Catch::Approx(9.0f / 32768.0f));
    CHECK(decoded.rootKey == 84);
}

TEST_CASE("KRZ keymap entry layouts follow method and entry size", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    KeymapSpec a; a.id = 200; a.method = 0x03; a.entrySize = 3;
    KeymapSpec b; b.id = 201; b.method = 0x13; b.entrySize = 5;
    KeymapSpec c; c.id = 202; c.method = 0x0F; c.entrySize = 5;
    for (int key = 12; key < 128; ++key)
    {
        a.levels[0].push_back(entry(200));
        b.levels[0].push_back(entry(201, 2, -250));
        c.levels[0].push_back(entry(202, 3, 25, -6));
    }
    spec.keymaps = { a, b, c };
    auto file = bankFile(dir, "keymaps.krz", spec);

    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    REQUIRE(bank.keymaps.size() == 3);
    const auto& first = bank.keymaps[0].levels[0][60];
    CHECK(first.sampleId == 200);
    CHECK(first.tuningCents == 0);
    const auto& second = bank.keymaps[1].levels[0][60];
    CHECK(second.sampleId == 201);
    CHECK(second.subSample == 2);
    CHECK(second.tuningCents == -250);
    const auto& third = bank.keymaps[2].levels[0][60];
    CHECK(third.sampleId == 202);
    CHECK(third.subSample == 3);
    CHECK(third.tuningCents == 25);
    CHECK(third.volumeHalfDb == -6);
    CHECK(bank.keymaps[0].levels[0][11].sampleId == 0); // notes below C0 are not mapped
}

TEST_CASE("KRZ keymap velocity levels land in their own bands", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    KeymapSpec k; k.id = 200;
    for (int level = 0; level < 8; ++level)
        for (int key = 12; key < 128; ++key)
            k.levels[(size_t) level].push_back(entry(level < 5 ? 200 : 201));
    spec.keymaps = { k };
    auto file = bankFile(dir, "levels.krz", spec);

    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    for (int level = 0; level < 8; ++level)
        CHECK(bank.keymaps[0].levels[(size_t) level][60].sampleId == (level < 5 ? 200 : 201));
}

TEST_CASE("KRZ program layers report their keymaps", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.keymaps = { fullKeymap(200, 200), fullKeymap(201, 201) };
    ProgramSpec program; program.id = 200; program.name = "Two Layers";
    program.layers = { LayerSpec { 200, 0 }, LayerSpec { 201, 0 } };
    spec.programs = { program };
    auto file = bankFile(dir, "program.krz", spec);

    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    REQUIRE(bank.programs.size() == 1);
    CHECK(bank.programs[0].name == "Two Layers");
    REQUIRE(bank.programs[0].layers.size() == 2);
    CHECK(bank.programs[0].layers[0].keymap1 == 200);
    CHECK(bank.programs[0].layers[1].keymap1 == 201);
    CHECK(bank.programs[0].layers[0].keymap2 == 0);
}

TEST_CASE("KRZ split set joins its parts and resolves from either", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200; sample.name = "Split";
    HeaderSpec header; header.startWord = 2; header.endWord = 5;
    sample.headers = { header };
    spec.samples = { sample };
    spec.pcm = { 1, 2, 3, 4 };
    auto first = writeBank(dir.dir, "split.kr1.krz", spec);
    auto bytes = makeContinuation((uint32_t) 0, { 5, 6, 7, 8 });
    writeBytes(dir.dir, "split.kr2.krz", bytes);

    Bank bank; juce::String error;
    REQUIRE(readBank(first, bank, error));
    CHECK(bank.warnings.isEmpty());
    CHECK(bank.pcm.size() == 8);
    DecodedSample decoded;
    REQUIRE(decodeHeader(bank, bank.samples[0], 0, decoded, error));
    CHECK(decoded.audio->frames == 4);
    CHECK(decoded.audio->left[0] == Catch::Approx(3.0f / 32768.0f));
    CHECK(decoded.audio->left[3] == Catch::Approx(6.0f / 32768.0f));

    Bank fromSecond; juce::String secondError;
    REQUIRE(readBank(dir.dir.getChildFile("split.kr2.krz"), fromSecond, secondError));
    CHECK(fromSecond.pcm.size() == 8);
    CHECK(fromSecond.samples.size() == 1);
}

TEST_CASE("KRZ missing continuation part is explained and bounded", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200; sample.name = "Needs Part Two";
    HeaderSpec header; header.startWord = 0; header.endWord = 9;
    sample.headers = { header };
    spec.samples = { sample };
    spec.pcm = { 1, 2, 3, 4 };
    auto first = writeBank(dir.dir, "gone.kr1.krz", spec);

    Bank bank; juce::String error;
    REQUIRE(readBank(first, bank, error));
    CHECK(bank.pcm.size() == 4);
    CHECK(bank.warnings.joinIntoString("|").contains("gone.kr2.krz"));
    DecodedSample decoded; juce::String decodeError;
    CHECK_FALSE(decodeHeader(bank, bank.samples[0], 0, decoded, decodeError));
    CHECK(decodeError.contains("gone.kr2.krz"));
}

TEST_CASE("KRZ continuation part naming accepts lower and upper case", "[krz]")
{
    CHECK(continuationPartNumber(juce::File("/x/A.KR2.KRZ")) == 2);
    CHECK(continuationPartNumber(juce::File("/x/a.kr1.k25")) == 1);
    CHECK(continuationPartNumber(juce::File("/x/a.kr10.krz")) == 10);
    CHECK(continuationPartNumber(juce::File("/x/a.krz")) == 0);
    CHECK(continuationPartNumber(juce::File("/x/akr2.krz")) == 0);
    CHECK(continuationPartNumber(juce::File("/x/a.kr.krz")) == 0);
}

TEST_CASE("KRZ bank without sample data reports no PCM", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.keymaps = { fullKeymap(200, 0) };
    auto file = writeBank(dir.dir, "nopcm.krz", spec);
    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    CHECK_FALSE(bank.hasPcm());
    CHECK(bank.pcm.empty());
}

TEST_CASE("KRZ corrupt files are rejected with a reason", "[krz]")
{
    TestDirectory dir;
    juce::String error;

    SECTION("not a Kurzweil bank")
    {
        std::vector<std::byte> bytes(64, std::byte { 0 });
        auto file = writeBytes(dir.dir, "noise.krz", bytes);
        Bank bank;
        CHECK_FALSE(readBank(file, bank, error));
        CHECK(error.isNotEmpty());
    }
    SECTION("forged block lengths")
    {
        BankSpec spec;
        spec.keymaps = { fullKeymap(200, 0) };
        auto bytes = makeBank(spec);
        // The first block's negative length is the first object word: point it backwards past EOF.
        bytes[0x20] = std::byte { 0x80 };
        bytes[0x21] = std::byte { 0x00 };
        bytes[0x22] = std::byte { 0x00 };
        bytes[0x23] = std::byte { 0x00 };
        auto file = writeBytes(dir.dir, "forged.krz", bytes);
        Bank bank;
        CHECK_FALSE(readBank(file, bank, error));
        CHECK(error.isNotEmpty());
    }
    SECTION("PCM offset past the end of file")
    {
        BankSpec spec;
        spec.keymaps = { fullKeymap(200, 0) };
        auto bytes = makeBank(spec);
        bytes[4] = std::byte { 0x7f };
        bytes[5] = std::byte { 0xff };
        bytes[6] = std::byte { 0xff };
        bytes[7] = std::byte { 0xff };
        auto file = writeBytes(dir.dir, "osize.krz", bytes);
        Bank bank;
        CHECK_FALSE(readBank(file, bank, error));
        CHECK(error.isNotEmpty());
    }
    SECTION("sample span past the PCM region")
    {
        BankSpec spec;
        SampleSpec sample; sample.id = 200;
        HeaderSpec header; header.startWord = 0; header.endWord = 99;
        sample.headers = { header };
        spec.samples = { sample };
        spec.pcm = { 1, 2, 3, 4 };
        auto file = writeBank(dir.dir, "overshoot.krz", spec);
        Bank bank;
        REQUIRE(readBank(file, bank, error));
        DecodedSample decoded; juce::String decodeError;
        CHECK_FALSE(decodeHeader(bank, bank.samples[0], 0, decoded, decodeError));
        CHECK(decodeError.isNotEmpty());
    }
    SECTION("SROM is recognised and refused")
    {
        std::vector<std::byte> bytes(32, std::byte { 0 });
        const char magic[4] = { 'S', 'R', 'O', 'M' };
        for (int i = 0; i < 4; ++i) bytes[(size_t) i] = (std::byte) magic[i];
        auto file = writeBytes(dir.dir, "fx.krz", bytes);
        CHECK(isKrzFile(file));
        Bank bank;
        CHECK_FALSE(readBank(file, bank, error));
        CHECK(error.containsIgnoreCase("SROM"));
    }
}

TEST_CASE("KRZ format probe accepts the extensions and the magic", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.keymaps = { fullKeymap(200, 0) };
    CHECK(isKrzFile(writeBank(dir.dir, "a.krz", spec)));
    CHECK(isKrzFile(writeBank(dir.dir, "b.k25", spec)));
    CHECK(isKrzFile(writeBank(dir.dir, "c.k26", spec)));
    CHECK_FALSE(isKrzFile(writeBytes(dir.dir, "d.krz", std::vector<std::byte>(32, std::byte { 0 }))));
}

TEST_CASE("KRZ block with several objects is walked to the end", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    KeymapSpec a = fullKeymap(200, 200);
    KeymapSpec b = fullKeymap(201, 201);
    KeymapSpec c = fullKeymap(202, 202);
    spec.keymaps = { a, b, c };
    spec.packObjects = true;
    auto file = writeBank(dir.dir, "packed.krz", spec);
    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    CHECK(bank.keymaps.size() == 3);
    REQUIRE(bank.keymaps.size() == 3);
    CHECK(bank.keymaps[2].levels[0][60].sampleId == 202);
}

TEST_CASE("KRZ object with a forged name offset stays inside the block", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.keymaps = { fullKeymap(200, 200) };
    RawObject raw;
    raw.type = 24;
    raw.id = 7;
    raw.name = "Effect";
    raw.body.assign(8, std::byte { 0 });
    raw.forcedNameOfs = 0x7ff0;
    spec.raw = { raw };
    auto file = writeBank(dir.dir, "forgedname.krz", spec);
    Bank bank; juce::String error;
    const bool read = readBank(file, bank, error);
    INFO("error: " << error);
    REQUIRE(read);
    CHECK(bank.keymaps.size() == 1);
}

TEST_CASE("KRZ keymap whose entry size cannot hold its method is skipped", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    KeymapSpec broken = fullKeymap(200, 200, 0x13, 1);
    spec.keymaps = { broken, fullKeymap(201, 201) };
    auto file = writeBank(dir.dir, "shortentry.krz", spec);
    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    CHECK(bank.keymaps.size() == 1);
    CHECK(bank.keymaps[0].id == 201);
    CHECK(bank.warnings.joinIntoString("|").contains("200"));
}

TEST_CASE("KRZ short keymap repeats its last entry to the top of the keyboard", "[krz]")
{
    TestDirectory dir;
    BankSpec spec;
    KeymapSpec keymap; keymap.id = 200; keymap.slotCount = 97;
    keymap.levels[0].assign(97, entry(200));
    spec.keymaps = { keymap };
    auto file = writeBank(dir.dir, "short.krz", spec);
    Bank bank; juce::String error;
    REQUIRE(readBank(file, bank, error));
    REQUIRE(bank.keymaps.size() == 1);
    CHECK(bank.keymaps[0].levels[0][120].sampleId == 200);
    CHECK(bank.keymaps[0].levels[0][127].sampleId == 200);
}

TEST_CASE("KRZ file shorter than its header is refused", "[krz]")
{
    TestDirectory dir;
    std::vector<std::byte> bytes { std::byte { 'P' }, std::byte { 'R' }, std::byte { 'A' } };
    auto file = writeBytes(dir.dir, "tiny.krz", bytes);
    Bank bank; juce::String error;
    CHECK_FALSE(readBank(file, bank, error));
    CHECK(error.isNotEmpty());
}
