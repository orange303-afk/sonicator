#include "AssetAnalyzer.h"
#include "Rex2Metadata.h"
#include "core/sampler/FileInstrument.h"
#include <cmath>
namespace sonicator {
namespace {
int bitsFromRexFormat(int format)
{
    switch (format)
    {
        case 1: return 8;
        case 3: return 16;
        case 5: return 24;
        case 7: return 32;
        default: return 0;
    }
}
bool isSoundFont(const juce::File& file)
{
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) return false;
    char header[12] {};
    if (stream.read(header, 12) != 12) return false;
    return juce::String::fromUTF8(header, 4) == "RIFF"
        && juce::String::fromUTF8(header + 8, 4) == "sfbk";
}
} // namespace
SampleRecord analyzeMetadataAsset(const juce::File& file, CancelCheck cancel)
{
    SampleRecord r;
    r.filePath = file.getFullPathName();
    r.fileName = file.getFileName();
    r.format = file.getFileExtension().trimCharactersAtStart(".").toUpperCase();
    r.fileSize = file.getSize();
    r.dateModified = file.getLastModificationTime().toMilliseconds();
    r.dateAnalyzed = juce::Time::currentTimeMillis();
    r.version = metadataOnlyVersion;
    if (r.format == "RX2")
    {
        const auto metadata = extractRex2Metadata(file);
        if (metadata.error.isNotEmpty()) { r.error = metadata.error; return r; }
        if (metadata.info.sampleRate > 0 && metadata.info.frames > 0)
            r.duration = (double) metadata.info.frames / metadata.info.sampleRate;
        r.sampleRate = metadata.info.sampleRate;
        r.numChannels = metadata.info.channels;
        r.bitsPerSample = bitsFromRexFormat(metadata.info.format);
    }
    else if (r.format == "SF2")
    {
        if (! isSoundFont(file)) { r.error = "Not a SoundFont 2 bank"; return r; }
    }
    else if (r.format == "EXS")
    {
        if (! isExs24File(file)) { r.error = "Not a supported EXS24 instrument"; return r; }
    }
    else if (r.format == "NKI")
    {
        juce::String detail;
        if (! isNkiFile(file, detail)) { r.error = "Not a recognised NKI instrument"; return r; }
        if (detail.containsIgnoreCase("unsupported")) r.error = detail;
    }
    else
    {
        r.error = "Unsupported metadata-only format";
        return r;
    }
    try
    {
        r.fileHash = FileScanner::computeFileHash(file, cancel);
        if (cancel && cancel()) { r.error = "Cancelled"; return r; }
    }
    catch (const std::exception& e)
    {
        r.error = juce::String::fromUTF8(e.what());
        return r;
    }
    return r;
}
} // namespace sonicator
