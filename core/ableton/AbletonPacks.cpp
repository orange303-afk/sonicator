#include "AbletonPacks.h"
#include "core/database/SampleRepository.h"
#include "core/settings/AppSettings.h"
#include <algorithm>
#include <cstring>
namespace sonicator {
namespace {
juce::uint32 be32(const unsigned char* p)
{
    return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16)
         | ((juce::uint32) p[2] << 8) | p[3];
}
}
AbletonPreviewInfo abletonPreviewInfo(const juce::File& file)
{
    AbletonPreviewInfo info;
    if (! file.hasFileExtension("ogg")) return info;
    const auto path = file.getFullPathName().replaceCharacter('\\', '/');
    if (! path.containsIgnoreCase("/Ableton Folder Info/Previews/")) return info;
    info.isPreview = true;
    auto stem = file.getFileNameWithoutExtension();
    const auto kindExtension = stem.fromLastOccurrenceOf(".", true, false).toLowerCase();
    if (kindExtension == ".alc") info.kind = "Clip";
    else if (kindExtension == ".adg") info.kind = "Rack";
    else if (kindExtension == ".adv") info.kind = "Device";
    else info.kind = "Preview";
    if (info.kind != "Preview") stem = stem.dropLastCharacters(kindExtension.length());
    info.source = stem.trim();
    return info;
}
bool isAbletonProtectedSample(const juce::File& file)
{
    if (! file.hasFileExtension("aif;aiff;aifc")) return false;
    juce::FileInputStream input(file);
    if (! input.openedOk()) return false;
    unsigned char header[12] {};
    if (input.read(header, 12) != 12) return false;
    if (std::memcmp(header, "FORM", 4) != 0 || std::memcmp(header + 8, "AIFC", 4) != 0) return false;
    const auto declared = 8 + (int64_t) be32(header + 4);
    const auto end = juce::jmin(declared, input.getTotalLength());
    // A malformed AIFC must not turn discovery into a long seek loop over zero-size chunks.
    static constexpr int maxChunks = 1024;
    int visited = 0;
    for (int64_t position = 12; position + 8 <= end;)
    {
        if (++visited > maxChunks) return false;
        input.setPosition(position);
        unsigned char chunk[8] {};
        if (input.read(chunk, 8) != 8) break;
        const auto size = (int64_t) be32(chunk + 4);
        if (size > end - position - 8) break;
        if (std::memcmp(chunk, "COMM", 4) == 0)
        {
            if (size < 22) return false;
            unsigned char compression[4] {};
            input.setPosition(position + 8 + 18);
            if (input.read(compression, 4) != 4) return false;
            return std::memcmp(compression, "able", 4) == 0;
        }
        position += 8 + size + (size & 1);
    }
    return false;
}
bool registerAbletonPacks(SampleRepository& repo, const SettingsStore& settings, const juce::File& root)
{
    if (settings.load().abletonPacksAdded || ! root.isDirectory()) return false;
    const auto roots = repo.roots();
    const bool covered = std::any_of(roots.begin(), roots.end(), [&](const auto& registered)
    { return root == juce::File(registered) || root.isAChildOf(juce::File(registered)); });
    if (! covered) repo.addRoot(root.getFullPathName());
    settings.update([](AppSettings& saved) { saved.abletonPacksAdded = true; });
    return ! covered;
}
}
