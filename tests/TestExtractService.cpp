#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/extract/ExtractService.h"
#include "TestHelpers.h"
#include <algorithm>
#include <cstring>
#include <cmath>
using namespace sonicator;
namespace {
struct Blob
{
    std::vector<juce::uint8> v;
    void u8(int x) { v.push_back((juce::uint8) x); }
    void u16be(int x) { u8((x >> 8) & 0xff); u8(x & 0xff); }
    void u16(juce::uint16 x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
    void u32(juce::uint32 x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); }
    void ascii(const juce::String& text, size_t width)
    {
        const auto start = v.size();
        auto utf = text.toRawUTF8();
        const int n = juce::jmin((int) width, (int) text.getNumBytesAsUTF8());
        v.insert(v.end(), utf, utf + n);
        while (v.size() < start + width) v.push_back(0);
    }
};
void chunk(Blob& out, const char* id, const Blob& payload)
{
    out.ascii(id, 4); out.u32((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.u8(0);
}
// Slots: 0 "tone" (64 bytes), 1 empty, 2 "silence" (16 bytes of zero PCM).
juce::File writeMod(TestDirectory& t)
{
    Blob b;
    b.ascii("Test MOD", 20);
    const int lengths[31] = { 32, 0, 8 };
    const char* names[31] = { "tone", "", "silence" };
    for (int i = 0; i < 31; ++i)
    {
        b.ascii(names[i], 22);
        b.u16be(lengths[i]); b.u8(0); b.u8(64); b.u16be(0); b.u16be(1);
    }
    b.u8(1); b.u8(0);
    for (int i = 0; i < 128; ++i) b.u8(0);
    b.ascii("M.K.", 4);
    for (int i = 0; i < 4 * 64 * 4; ++i) b.u8(0);
    for (int i = 0; i < 64; ++i) b.u8((int8_t) (i < 32 ? i * 2 : (63 - i) * 2));
    for (int i = 0; i < 16; ++i) b.u8(0);
    auto file = t.dir.getChildFile("song.mod");
    file.replaceWithData(b.v.data(), b.v.size());
    return file;
}
juce::File writeBank(TestDirectory& t, int sampleCount, int badRateIndex = -1)
{
    Blob pcm;
    for (int i = 0; i < sampleCount * 64; ++i)
        pcm.u16((juce::uint16) (juce::int16) std::lround(
            0.5 * 32767.0 * std::sin(2.0 * juce::MathConstants<double>::pi * 100.0 * i / 44100.0)));
    Blob smpl; chunk(smpl, "smpl", pcm);
    Blob sdta; sdta.ascii("sdta", 4); sdta.v.insert(sdta.v.end(), smpl.v.begin(), smpl.v.end());
    Blob info; info.ascii("INFO", 4);
    Blob pbag; pbag.u16(0); pbag.u16(0); pbag.u16(1); pbag.u16(0);
    Blob pgen; pgen.u16(41); pgen.u16(0);
    Blob ibag; ibag.u16(0); ibag.u16(0); ibag.u16(2); ibag.u16(0); ibag.u16(2); ibag.u16(0);
    Blob igen; igen.u16(43); igen.u16((juce::uint16) (0 | (127 << 8))); igen.u16(53); igen.u16(0);
    Blob inst; inst.ascii("Kit", 20); inst.u16(0);
    Blob instEnd; instEnd.ascii("EOT", 20); instEnd.u16(2);
    inst.v.insert(inst.v.end(), instEnd.v.begin(), instEnd.v.end());
    Blob shdr;
    for (int i = 0; i < sampleCount; ++i)
    {
        const auto start = (juce::uint32) (i * 64), end = (juce::uint32) (i * 64 + 64);
        Blob h; h.ascii("s" + juce::String(i + 1), 20);
        h.u32(start); h.u32(end); h.u32(start); h.u32(end);
        h.u32(i == badRateIndex ? 0 : 44100);
        h.u8(60); h.u8(0); h.u16(0); h.u16(1);
        shdr.v.insert(shdr.v.end(), h.v.begin(), h.v.end());
    }
    Blob shdrEnd; shdrEnd.ascii("EOS", 20); for (int i = 0; i < 26; ++i) shdrEnd.u8(0);
    shdr.v.insert(shdr.v.end(), shdrEnd.v.begin(), shdrEnd.v.end());
    Blob pdta; pdta.ascii("pdta", 4);
    Blob phdr;
    {
        Blob a; a.ascii("Kit", 20); a.u16(0); a.u16(128); a.u16(0); a.u32(0); a.u32(0); a.u32(0);
        Blob b; b.ascii("EOP", 20); b.u16(0); b.u16(0); b.u16(1); b.u32(0); b.u32(0); b.u32(0);
        phdr.v.insert(phdr.v.end(), a.v.begin(), a.v.end());
        phdr.v.insert(phdr.v.end(), b.v.begin(), b.v.end());
    }
    chunk(pdta, "phdr", phdr); chunk(pdta, "pbag", pbag); chunk(pdta, "pgen", pgen);
    chunk(pdta, "inst", inst); chunk(pdta, "ibag", ibag); chunk(pdta, "igen", igen); chunk(pdta, "shdr", shdr);
    Blob infoList; infoList.ascii("LIST", 4); infoList.u32((juce::uint32) info.v.size());
    infoList.v.insert(infoList.v.end(), info.v.begin(), info.v.end());
    Blob sdtaList; sdtaList.ascii("LIST", 4); sdtaList.u32((juce::uint32) sdta.v.size());
    sdtaList.v.insert(sdtaList.v.end(), sdta.v.begin(), sdta.v.end());
    Blob pdtaList; pdtaList.ascii("LIST", 4); pdtaList.u32((juce::uint32) pdta.v.size());
    pdtaList.v.insert(pdtaList.v.end(), pdta.v.begin(), pdta.v.end());
    Blob body; body.ascii("sfbk", 4);
    body.v.insert(body.v.end(), infoList.v.begin(), infoList.v.end());
    body.v.insert(body.v.end(), sdtaList.v.begin(), sdtaList.v.end());
    body.v.insert(body.v.end(), pdtaList.v.begin(), pdtaList.v.end());
    Blob file; file.ascii("RIFF", 4); file.u32((juce::uint32) body.v.size());
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile("bank.sf2");
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
std::vector<juce::File> wavsIn(const juce::File& folder)
{
    std::vector<juce::File> out;
    if (! folder.isDirectory()) return out;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*.wav",
                                                             juce::File::findFiles))
        out.push_back(entry.getFile());
    std::sort(out.begin(), out.end(), [](const juce::File& a, const juce::File& b)
        { return a.getFileName() < b.getFileName(); });
    return out;
}
} // namespace

TEST_CASE("Extract writes sample folders and counts empty and silent entries", "[extract][service]")
{
    TestDirectory t;
    auto module = writeMod(t);
    ExtractService service;
    ExtractOptions options;
    options.sameFolder = true;
    REQUIRE(service.start({ module }, options));
    REQUIRE(service.waitUntilFinished(20000));
    const auto progress = service.progress();
    REQUIRE_FALSE(progress.running);
    REQUIRE(progress.written == 1);
    REQUIRE(progress.skippedEmpty == 29);
    REQUIRE(progress.skippedSilent == 1);
    REQUIRE(progress.failed == 0);
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == ExtractFileState::extracted);
    REQUIRE(results[0].empty == 29);
    REQUIRE(results[0].silent == 1);
    const auto folder = t.dir.getChildFile("song samples");
    REQUIRE(folder.isDirectory());
    REQUIRE(results[0].folder == folder);
    const auto files = wavsIn(folder);
    REQUIRE(files.size() == 1);
    REQUIRE(files[0].getFileName() == "001 tone.wav");
    REQUIRE(files[0].existsAsFile());
}

TEST_CASE("Extract honours a chosen output folder and suffixes an existing sample folder", "[extract][service]")
{
    TestDirectory t;
    auto bank = writeBank(t, 2);
    ExtractService service;
    ExtractOptions options;
    options.sameFolder = false;
    options.outputFolder = t.dir.getChildFile("output");
    REQUIRE(service.start({ bank }, options));
    REQUIRE(service.waitUntilFinished(20000));
    REQUIRE(service.results().size() == 1);
    const auto first = t.dir.getChildFile("output/bank samples");
    REQUIRE(first.isDirectory());
    REQUIRE(wavsIn(first).size() == 2);
    REQUIRE(wavsIn(first)[0].getFileName() == "001 s1.wav");
    REQUIRE(wavsIn(first)[1].getFileName() == "002 s2.wav");

    ExtractService second;
    REQUIRE(second.start({ bank }, options));
    REQUIRE(second.waitUntilFinished(20000));
    const auto secondFolder = t.dir.getChildFile("output/bank samples 2");
    REQUIRE(secondFolder.isDirectory());
    REQUIRE(wavsIn(secondFolder).size() == 2);
    REQUIRE(wavsIn(first).size() == 2);
}

TEST_CASE("Extract normalises only when asked", "[extract][service]")
{
    TestDirectory t;
    auto bank = writeBank(t, 1);
    ExtractService plain;
    ExtractOptions options;
    options.sameFolder = true;
    REQUIRE(plain.start({ bank }, options));
    REQUIRE(plain.waitUntilFinished(20000));
    REQUIRE(plain.progress().written == 1);
    options.normalize = true;
    ExtractService normalised;
    REQUIRE(normalised.start({ bank }, options));
    REQUIRE(normalised.waitUntilFinished(20000));
    REQUIRE(normalised.progress().written == 1);
    // A quieter copy of the same bank proves the second run scaled its output.
    const auto plainFile = wavsIn(t.dir.getChildFile("bank samples"))[0];
    const auto normalisedFile = wavsIn(t.dir.getChildFile("bank samples 2"))[0];
    REQUIRE(plainFile.existsAsFile());
    REQUIRE(normalisedFile.existsAsFile());
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> before(formats.createReaderFor(plainFile));
    std::unique_ptr<juce::AudioFormatReader> after(formats.createReaderFor(normalisedFile));
    REQUIRE(before != nullptr); REQUIRE(after != nullptr);
    juce::AudioBuffer<float> a(1, 64), b(1, 64);
    REQUIRE(before->read(&a, 0, 64, 0, true, true));
    REQUIRE(after->read(&b, 0, 64, 0, true, true));
    REQUIRE(b.getMagnitude(0, 64) > a.getMagnitude(0, 64));
}

TEST_CASE("Extract keeps going after an unsupported file and reports it", "[extract][service]")
{
    TestDirectory t;
    auto module = writeMod(t);
    auto unsupported = t.wave("plain.wav");
    ExtractService service;
    REQUIRE(service.start({ unsupported, module }, ExtractOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].state == ExtractFileState::failed);
    REQUIRE(results[0].error.containsIgnoreCase("MOD"));
    REQUIRE(results[1].state == ExtractFileState::extracted);
    REQUIRE(service.progress().written == 1);
}

TEST_CASE("Extract cancellation keeps finished files and leaves no partial file", "[extract][service]")
{
    TestDirectory t;
    auto bank = writeBank(t, 40);
    ExtractService service;
    REQUIRE(service.start({ bank }, ExtractOptions{}));
    service.cancel();
    REQUIRE(service.waitUntilFinished(20000));
    const auto progress = service.progress();
    REQUIRE(progress.cancelled);
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == ExtractFileState::cancelled);
    for (const auto& file : wavsIn(t.dir.getChildFile("bank samples")))
        REQUIRE(file.getSize() > 0);
    for (const auto& entry : juce::RangedDirectoryIterator(t.dir, true, "*.part",
                                                             juce::File::findFiles))
        FAIL("partial file left behind: " << entry.getFile().getFullPathName());
}

TEST_CASE("Extract file names are numbered, sanitised and keep non-ASCII text", "[extract][service]")
{
    REQUIRE(extractSampleFileName(7, "Kick 01") == "007 Kick 01.wav");
    REQUIRE(extractSampleFileName(12, juce::String::fromUTF8("Бочка"))
            == juce::String::fromUTF8("012 Бочка.wav"));
    const auto unsafe = extractSampleFileName(3, "../a:b*c?");
    REQUIRE_FALSE(unsafe.containsChar('/'));
    REQUIRE_FALSE(unsafe.containsChar(':'));
    REQUIRE_FALSE(unsafe.containsChar('*'));
    REQUIRE_FALSE(unsafe.containsChar('?'));
    REQUIRE(unsafe.startsWith("003 "));
    REQUIRE(unsafe.endsWith(".wav"));
    REQUIRE(extractSampleFileName(1, "") == "001 Sample.wav");
    REQUIRE(extractSampleFileName(1, "Uhhuhu.wav") == "001 Uhhuhu.wav");
    REQUIRE(extractSampleFileName(2, "kick.WAV") == "002 kick.wav");
    REQUIRE(extractSampleFileName(3, " .wav ") == "003 Sample.wav");
}

TEST_CASE("Extract keeps a non-ASCII source name in the sample folder", "[extract][service]")
{
    TestDirectory t;
    auto module = writeMod(t);
    const auto renamed = t.dir.getChildFile(juce::String::fromUTF8("Бочка.mod"));
    REQUIRE(module.moveFileTo(renamed));
    ExtractService service;
    REQUIRE(service.start({ renamed }, ExtractOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    REQUIRE(service.results().size() == 1);
    REQUIRE(service.results()[0].folder
            == t.dir.getChildFile(juce::String::fromUTF8("Бочка samples")));
    REQUIRE(wavsIn(service.results()[0].folder).size() == 1);
}

TEST_CASE("Cancelling mid-file keeps finished WAVs and reports no sample errors", "[extract][service]")
{
    TestDirectory t;
    auto bank = writeBank(t, 600);
    ExtractService service;
    REQUIRE(service.start({ bank }, ExtractOptions{}));
    for (int spins = 0; spins < 5000 && service.progress().written == 0; ++spins)
        juce::Thread::sleep(1);
    service.cancel();
    REQUIRE(service.waitUntilFinished(20000));
    const auto progress = service.progress();
    REQUIRE(progress.cancelled);
    REQUIRE(progress.written >= 1);
    REQUIRE(progress.failed == 0);
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == ExtractFileState::cancelled);
    REQUIRE(results[0].failed == 0);
    REQUIRE(results[0].problems.isEmpty());
    const auto files = wavsIn(results[0].folder);
    REQUIRE_FALSE(files.empty());
    for (const auto& file : files) REQUIRE(file.getSize() > 0);
    for (const auto& entry : juce::RangedDirectoryIterator(results[0].folder, true, "*.part",
                                                             juce::File::findFiles))
        FAIL("partial file left behind: " << entry.getFile().getFullPathName());
}

TEST_CASE("A corrupt embedded source does not stop the batch", "[extract][service]")
{
    TestDirectory t;
    auto broken = t.dir.getChildFile("broken.sf2");
    broken.replaceWithText("this is not a SoundFont");
    auto module = writeMod(t);
    ExtractService service;
    REQUIRE(service.start({ broken, module }, ExtractOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].state == ExtractFileState::failed);
    REQUIRE(results[0].error.isNotEmpty());
    REQUIRE(results[1].state == ExtractFileState::extracted);
    REQUIRE(results[1].written == 1);
    REQUIRE(service.progress().written == 1);
}

TEST_CASE("One undecodable sample is reported and its siblings are still written", "[extract][service]")
{
    TestDirectory t;
    auto bank = writeBank(t, 2, 1);
    ExtractService service;
    REQUIRE(service.start({ bank }, ExtractOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == ExtractFileState::extracted);
    REQUIRE(results[0].written == 1);
    REQUIRE(results[0].failed == 1);
    REQUIRE(results[0].problems.size() == 1);
    REQUIRE(results[0].problems[0].contains("s2"));
    REQUIRE(wavsIn(results[0].folder).size() == 1);
    REQUIRE(service.progress().failed == 1);
}

TEST_CASE("An output path that is a file fails the source without writing", "[extract][service]")
{
    TestDirectory t;
    auto module = writeMod(t);
    auto notAFolder = t.dir.getChildFile("output.wav");
    notAFolder.replaceWithText("occupied");
    ExtractService service;
    ExtractOptions options;
    options.sameFolder = false;
    options.outputFolder = notAFolder;
    REQUIRE(service.start({ module }, options));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == ExtractFileState::failed);
    REQUIRE(results[0].error.containsIgnoreCase("output folder"));
    REQUIRE(results[0].written == 0);
    REQUIRE(notAFolder.loadFileAsString() == "occupied");
}

TEST_CASE("A hostile sample name stays inside the destination folder", "[extract][service]")
{
    TestDirectory t;
    auto bank = writeBank(t, 1);
    // Rewrite the first shdr name in place: 20 bytes at the start of the shdr chunk data.
    juce::MemoryBlock data;
    REQUIRE(t.dir.getChildFile("bank.sf2").loadFileAsData(data));
    auto* bytes = static_cast<char*>(data.getData());
    const std::string marker("shdr");
    const auto position = std::search(bytes, bytes + data.getSize(), marker.begin(), marker.end());
    REQUIRE(position != bytes + data.getSize());
    const auto nameOffset = (size_t) (position - bytes) + 8;
    const char hostile[] = "../a:b*c?";
    std::memcpy(bytes + nameOffset, hostile, sizeof(hostile) - 1);
    for (size_t i = nameOffset + sizeof(hostile) - 1; i < nameOffset + 20; ++i) bytes[i] = 0;
    REQUIRE(t.dir.getChildFile("bank.sf2").replaceWithData(data.getData(), data.getSize()));
    ExtractService service;
    REQUIRE(service.start({ t.dir.getChildFile("bank.sf2") }, ExtractOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].written == 1);
    const auto files = wavsIn(results[0].folder);
    REQUIRE(files.size() == 1);
    REQUIRE(files[0].getParentDirectory() == results[0].folder);
    const auto name = files[0].getFileName();
    REQUIRE_FALSE(name.containsChar('/'));
    REQUIRE_FALSE(name.containsChar(':'));
    REQUIRE_FALSE(name.containsChar('*'));
    REQUIRE_FALSE(name.containsChar('?'));
}
