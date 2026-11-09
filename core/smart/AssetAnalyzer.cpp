#include "AssetAnalyzer.h"
#include "Rex2Metadata.h"
#include "core/legacy/LegacyFormatProbe.h"
#include "core/disc/RawDiscSource.h"
#include "core/disc/DiscImageProbe.h"
#include "core/disc/CueSheet.h"
#include "core/sampler/FileInstrument.h"
#include "core/reason/ReasonPatch.h"
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
    if (file.hasFileExtension("cmb;repatch;rfl"))
    {
        const auto patch=readReasonPatch(file, false);
        if (patch.error.isNotEmpty())
        { r.error=patch.error; r.compatibility=AssetCompatibility::invalid; return r; }
        if (r.format=="RFL")
        {
            r.compatibility=AssetCompatibility::supported; // recognised metadata card; not audio support
            return r; // No archive-body reads or full-file hashing for multi-gigabyte ReFills.
        }
    }
    else if (isLegacyTrackerExtension(file))
    {
        const auto probe = probeLegacyTracker(file);
        if (! probe.recognised)
        {
            r.error = probe.error; r.compatibility = AssetCompatibility::invalid; return r;
        }
        // The detected format wins over the extension: a renamed S3M is catalogued as S3M.
        r.format = trackerFormatName(probe.format);
        r.numChannels = probe.channels;
    }
    else if (r.format == "RX2")
    {
        const auto metadata = extractRex2Metadata(file);
        if (metadata.error.isNotEmpty())
        {
            r.error = metadata.error; r.compatibility = AssetCompatibility::invalid; return r;
        }
        if (metadata.info.sampleRate > 0 && metadata.info.frames > 0)
            r.duration = (double) metadata.info.frames / metadata.info.sampleRate;
        r.sampleRate = metadata.info.sampleRate;
        r.numChannels = metadata.info.channels;
        r.bitsPerSample = bitsFromRexFormat(metadata.info.format);
    }
    else if (r.format == "SF2")
    {
        if (! isSoundFont(file))
        {
            r.error = "Not a SoundFont 2 bank"; r.compatibility = AssetCompatibility::invalid; return r;
        }
    }
    else if (r.format == "EXS")
    {
        if (! isExs24File(file))
        {
            r.error = "Not a supported EXS24 instrument"; r.compatibility = AssetCompatibility::invalid; return r;
        }
    }
    else if (r.format == "CUE")
    {
        const auto sheet = openCueSheet(file, {}, cancel);
        if (! sheet.ok())
        {
            r.error = sheet.error;
            r.compatibility = sheet.status == DiscStatus::unsupported ? AssetCompatibility::unsupported
                                                                      : AssetCompatibility::invalid;
            return r;
        }
        if (sheet.value.dataTrackIndices().empty())
        {
            r.error = "the cue sheet has no data track";
            r.compatibility = AssetCompatibility::unsupported;
            return r;
        }
        r.format = "CUE";
        r.compatibility = AssetCompatibility::supported;
    }
    else if (r.format == "ISO" || r.format == "IMG" || r.format == "BIN" || r.format == "MDX")
    {
        const auto raw = openRawDisc(file, {});
        if (! raw.ok())
        {
            r.error = raw.error; r.compatibility = AssetCompatibility::unknown; return r;
        }
        const auto probe = probeDisc(*raw.value, {}, cancel);
        if (probe.value.kind == DiscImageKind::mdx)
        {
            r.error = probe.error.isNotEmpty() ? probe.error : "MDX is not supported in this build";
            r.compatibility = AssetCompatibility::unsupported;
            return r;
        }
        if (! probe.ok())
        {
            r.error = probe.error.isNotEmpty() ? probe.error : "unrecognised disc image";
            r.compatibility = probe.status == DiscStatus::malformed ? AssetCompatibility::invalid
                                                                    : AssetCompatibility::unknown;
            return r;
        }
        // The detected layout wins over the extension, like the tracker probe.
        r.format = probe.value.kind == DiscImageKind::akaiRaw ? "AKAI" : "ISO";
        r.compatibility = AssetCompatibility::supported;
    }
    else if (r.format == "NKI")
    {
        juce::String detail;
        if (! isNkiFile(file, detail))
        {
            r.error = "Not a recognised NKI instrument"; r.compatibility = AssetCompatibility::invalid; return r;
        }
        if (detail.containsIgnoreCase("unsupported"))
        {
            r.error = detail; r.compatibility = AssetCompatibility::unsupported;
        }
    }
    else
    {
        r.error = "Unsupported metadata-only format";
        r.compatibility = AssetCompatibility::unsupported;
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
    if (r.error.isEmpty()) r.compatibility = AssetCompatibility::supported;
    return r;
}
} // namespace sonicator
