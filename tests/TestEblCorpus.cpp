#include <catch2/catch_test_macros.hpp>
#include "core/emu/EblReader.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <vector>
namespace {
juce::File environmentDirectory(const char* name)
{
    return juce::File(juce::SystemStats::getEnvironmentVariable(name, {}));
}
std::vector<juce::File> eblFilesUnder(const juce::File& root)
{
    auto found = root.findChildFiles(juce::File::findFiles, true, "*");
    found.removeIf([] (const juce::File& file) { return ! sonicator::isEblFile(file); });
    std::vector<juce::File> files(found.begin(), found.end());
    std::sort(files.begin(), files.end(),
              [] (const juce::File& a, const juce::File& b) { return a.getFullPathName() < b.getFullPathName(); });
    return files;
}
// The reference converter's file-name rule: [^A-Za-z0-9 ._+#-] becomes '_', then trimmed.
juce::String referenceSafeName(const juce::String& name)
{
    juce::String result;
    for (auto c : name)
    {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == ' ' || c == '.' || c == '_' || c == '+' || c == '#' || c == '-';
        result << (safe ? juce::String::charToString(c) : juce::String("_"));
    }
    result = result.trim();
    while (result.endsWithChar('.')) result = result.dropLastCharacters(1);
    return result.isEmpty() ? "unnamed" : result;
}
// The converter disambiguates same-named samples inside one bank as Name_2, Name_3.
juce::String stripUniquenessSuffix(const juce::String& stem)
{
    const int underscore = stem.lastIndexOfChar('_');
    if (underscore <= 0) return stem;
    const auto suffix = stem.substring(underscore + 1);
    if (suffix.length() < 1 || suffix.length() > 3 || ! suffix.containsOnly("0123456789")) return stem;
    return stem.substring(0, underscore);
}
juce::String bankFromPath(const juce::File& file, const juce::File& root)
{
    const auto relative = file.getRelativePathFrom(root).replaceCharacter('\\', '/');
    const auto parts = juce::StringArray::fromTokens(relative, "/", {});
    // <Bank>.exb/SamplePool/<file>.ebl -> the bank folder is the pool's parent.
    if (parts.size() >= 3 && parts[parts.size() - 2].equalsIgnoreCase("SamplePool"))
        return parts[parts.size() - 3];
    return file.getParentDirectory().getFileName();
}
juce::String referenceBank(const juce::File& file, const juce::File& referenceRoot)
{
    return file.getParentDirectory().getRelativePathFrom(referenceRoot).replaceCharacter('\\', '/');
}
// The data chunk of a reference WAV, checked to be 16-bit PCM.
bool readReferencePcm(const juce::File& file, std::vector<std::byte>& pcm, juce::String& error)
{
    auto stream = file.createInputStream();
    if (stream == nullptr) { error = "cannot open"; return false; }
    if (stream->readIntBigEndian() != (int) 0x52494646 /* RIFF */) { error = "not RIFF"; return false; }
    stream->readIntBigEndian();
    if (stream->readIntBigEndian() != (int) 0x57415645 /* WAVE */) { error = "not WAVE"; return false; }
    bool pcm16 = false;
    while (! stream->isExhausted())
    {
        const auto id = stream->readIntBigEndian();
        const auto size = (uint32_t) stream->readInt(); // RIFF chunk sizes are little-endian
        if (id == (int) 0x666d7420 /* fmt  */)
        {
            std::vector<std::byte> header(size);
            if (stream->read(header.data(), (int) size) != (int) size) { error = "short fmt"; return false; }
            if (size < 16) { error = "short fmt"; return false; }
            const auto format = (uint16_t) (std::to_integer<unsigned>(header[0]) | (std::to_integer<unsigned>(header[1]) << 8));
            const auto bits = (uint16_t) (std::to_integer<unsigned>(header[14]) | (std::to_integer<unsigned>(header[15]) << 8));
            pcm16 = format == 1 && bits == 16;
        }
        else if (id == (int) 0x64617461 /* data */)
        {
            if (! pcm16) { error = "not 16-bit PCM"; return false; }
            pcm.resize(size);
            if (stream->read(pcm.data(), (int) size) != (int) size) { error = "short data"; return false; }
            return true;
        }
        else
        {
            stream->skipNextBytes((int) size);
        }
        if (size % 2 == 1) stream->skipNextBytes(1);
    }
    error = "no data chunk";
    return false;
}
} // namespace

TEST_CASE("Every EBL in the real banks decodes", "[.ebl-corpus]")
{
    const auto root = environmentDirectory("SONICATOR_EBL_DIR");
    if (! root.isDirectory()) SKIP("SONICATOR_EBL_DIR is not set to a directory");
    const auto files = eblFilesUnder(root);
    REQUIRE_FALSE(files.empty());

    struct Snapshot { juce::int64 size = 0, modified = 0; };
    std::map<juce::String, Snapshot> sampled;
    const size_t step = std::max<size_t>(1, files.size() / 20);
    for (size_t i = 0; i < files.size(); i += step)
        sampled[files[i].getFullPathName()] = { files[i].getSize(),
                                                files[i].getLastModificationTime().toMilliseconds() };

    int mono = 0, stereo = 0, looped = 0, refused = 0;
    std::set<uint32_t> rates;
    std::map<juce::String, int> reasons;
    juce::StringArray firstRefusals;
    for (const auto& file : files)
    {
        const auto result = sonicator::readEblFile(file);
        if (! result.ok())
        {
            ++refused;
            ++reasons[result.error];
            if (firstRefusals.size() < 20) firstRefusals.add(file.getFileName() + ": " + result.error);
            continue;
        }
        result.sample->channels == 2 ? ++stereo : ++mono;
        if (result.sample->loop) ++looped;
        rates.insert(result.sample->sampleRate);
        CHECK(result.sample->pcm.size() == (size_t) (result.sample->frames * result.sample->channels));
    }
    std::cout << "EBL corpus: " << files.size() << " files, " << mono << " mono, " << stereo
              << " stereo, " << looped << " looped, " << rates.size() << " distinct rates, "
              << refused << " refused\n";
    for (const auto& [reason, count] : reasons)
        std::cout << "  refused (" << count << "): " << reason << "\n";
    for (const auto& line : firstRefusals) std::cout << "  " << line << "\n";
    for (const auto& [path, snapshot] : sampled)
    {
        const juce::File file(path);
        CHECK(file.getSize() == snapshot.size);
        CHECK(file.getLastModificationTime().toMilliseconds() == snapshot.modified);
    }
    REQUIRE(refused == 0);
}

TEST_CASE("EBL PCM matches samplerdisc byte for byte", "[.ebl-reference]")
{
    const auto root = environmentDirectory("SONICATOR_EBL_DIR");
    const auto referenceRoot = environmentDirectory("SONICATOR_EBL_REFERENCE");
    if (! root.isDirectory() || ! referenceRoot.isDirectory())
        SKIP("SONICATOR_EBL_DIR and SONICATOR_EBL_REFERENCE must be directories");
    const auto files = eblFilesUnder(root);
    REQUIRE_FALSE(files.empty());

    std::map<juce::String, std::vector<juce::File>> references;
    std::set<juce::String> duplicatedNames;
    for (const auto& file : referenceRoot.findChildFiles(juce::File::findFiles, true, "*.wav"))
    {
        const auto bank = referenceBank(file, referenceRoot);
        const auto stem = file.getFileNameWithoutExtension();
        references[bank + "/" + stem].push_back(file);
        // A Name_2 variant means two samples in this bank shared the name: the
        // reference cannot say which file is which, so such names are ambiguous.
        const auto base = stripUniquenessSuffix(stem);
        if (base != stem) duplicatedNames.insert(bank + "/" + base);
    }
    REQUIRE_FALSE(references.empty());

    int compared = 0, mismatches = 0, ambiguous = 0, unmatched = 0, skippedFormat = 0;
    juce::StringArray mismatchLines, unmatchedLines;
    for (const auto& file : files)
    {
        const auto decoded = sonicator::readEblFile(file);
        if (! decoded.ok()) continue; // the corpus test reports refusals
        const auto stem = decoded.sample->name.isNotEmpty() ? decoded.sample->name
                                                            : file.getFileNameWithoutExtension();
        const auto key = referenceSafeName(bankFromPath(file, root)) + "/" + referenceSafeName(stem);
        const auto found = references.find(key);
        if (found == references.end())
        {
            ++unmatched;
            if (unmatchedLines.size() < 20) unmatchedLines.add(key);
            continue;
        }
        if (duplicatedNames.count(key) != 0 || found->second.size() != 1)
        {
            ++ambiguous;
            continue;
        }
        std::vector<std::byte> reference;
        juce::String error;
        if (! readReferencePcm(found->second.front(), reference, error))
        {
            ++skippedFormat;
            continue;
        }
        ++compared;
        const auto expected = decoded.sample->frames * decoded.sample->channels * 2;
        const bool same = (int64_t) reference.size() == expected
            && std::memcmp(reference.data(), decoded.sample->pcm.data(), reference.size()) == 0;
        if (! same)
        {
            ++mismatches;
            if (mismatchLines.size() < 20)
                mismatchLines.add(found->second.front().getFullPathName() + " vs "
                                  + file.getFullPathName() + " (" + juce::String(expected)
                                  + " vs " + juce::String((int64_t) reference.size()) + " bytes)");
        }
    }
    std::cout << "EBL reference: " << compared << " compared, " << mismatches << " mismatches, "
              << ambiguous << " ambiguous names, " << unmatched << " unmatched, "
              << skippedFormat << " non-16-bit references, " << files.size() << " files\n";
    for (const auto& line : mismatchLines) std::cout << "  mismatch: " << line << "\n";
    for (const auto& line : unmatchedLines) std::cout << "  unmatched: " << line << "\n";
    REQUIRE(mismatches == 0);
    REQUIRE(compared * 10 >= (int) files.size() * 9);
}
