#include <catch2/catch_test_macros.hpp>
#include "core/disc/AkaiFilesystem.h"
#include "core/disc/DiscImageProbe.h"
#include "core/disc/RawDiscSource.h"
#include <cmath>
#include "core/scanner/FileScanner.h"
#include <juce_core/juce_core.h>
using namespace sonicator;

// Hidden by default ([.] tag). Set SONICATOR_DISC_IMAGE to a real read-only image; the test never
// writes to it and verifies size, mtime and SHA-256 after the catalogue pass.
TEST_CASE("Opt-in real disc image catalogue", "[.disc-corpus]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_IMAGE", {});
    REQUIRE(path.isNotEmpty());
    juce::File file(path);
    REQUIRE(file.existsAsFile());
    const auto size = file.getSize();
    const auto modified = file.getLastModificationTime();
    const auto hash = FileScanner::computeFileHash(file);

    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    const auto probe = probeDisc(*raw.value, {}, {});
    INFO(probe.error);
    REQUIRE(probe.ok());
    REQUIRE(probe.value.kind == DiscImageKind::akaiRaw);
    const auto opened = AkaiFilesystem::open(raw.value, {}, {});
    INFO(opened.error);
    REQUIRE(opened.ok());
    auto& fs = *opened.value;
    const auto partitions = fs.list(fs.root(), {});
    REQUIRE(partitions.ok());
    int volumes = 0, programs = 0, samples = 0, unsupported = 0, stereoPairs = 0;
    bool checkedStereo = false;
    for (const auto& partition : partitions.value.entries)
    {
        const auto listed = fs.list(partition.id, {});
        REQUIRE(listed.ok());
        for (const auto& volume : listed.value.entries)
        {
            ++volumes;
            const auto entries = fs.list(volume.id, {});
            REQUIRE(entries.ok());
            for (const auto& entry : entries.value.entries)
            {
                if (entry.kind == DiscEntryKind::file) ++samples;
                else if (entry.kind == DiscEntryKind::container) ++programs;
                else ++unsupported;
                if (entry.stereo)
                {
                    ++stereoPairs;
                    if (! checkedStereo)
                    {
                        const auto stereo = fs.decodeSample(entry.id, {});
                        INFO(entry.displayName << ": status=" << (int) stereo.status << " error=" << stereo.error);
                        REQUIRE(stereo.ok());
                        REQUIRE(stereo.value->channels == 2);
                        REQUIRE(stereo.value->frames > 0);
                        REQUIRE(stereo.value->right.size() == (size_t) stereo.value->frames);
                        checkedStereo = true;
                    }
                }
            }
        }
    }
    INFO("partitions=" << partitions.value.entries.size() << " volumes=" << volumes
         << " programs=" << programs << " samples=" << samples << " other=" << unsupported);
    REQUIRE(partitions.value.entries.size() == 13);
    // Decode the first verified samples and check the PCM is finite and audible.
    int decodedSamples = 0;
    for (const auto& partition : partitions.value.entries)
    {
        const auto listed = fs.list(partition.id, {});
        for (const auto& volume : listed.value.entries)
        {
            const auto entries = fs.list(volume.id, {});
            for (const auto& entry : entries.value.entries)
            {
                if (entry.kind != DiscEntryKind::file) continue;
                const auto decoded = fs.decodeSample(entry.id, {});
                INFO(entry.displayName << ": " << decoded.error);
                REQUIRE(decoded.ok());
                REQUIRE(decoded.value->frames > 0);
                REQUIRE(decoded.value->sampleRate == 44100);
                REQUIRE((decoded.value->channels == 1 || decoded.value->channels == 2));
                float peak = 0;
                for (float value : decoded.value->left)
                {
                    REQUIRE(std::isfinite(value));
                    REQUIRE(std::abs(value) <= 1.0f);
                    peak = juce::jmax(peak, std::abs(value));
                }
                if (decoded.value->channels == 2)
                {
                    REQUIRE(decoded.value->right.size() == (size_t) decoded.value->frames);
                    for (float value : decoded.value->right)
                    {
                        REQUIRE(std::isfinite(value));
                        REQUIRE(std::abs(value) <= 1.0f);
                        peak = juce::jmax(peak, std::abs(value));
                    }
                }
                REQUIRE(peak > 0.001f);
                if (++decodedSamples >= 20) break;
            }
            if (decodedSamples >= 20) break;
        }
        if (decodedSamples >= 20) break;
    }
    REQUIRE(decodedSamples == 20);
    REQUIRE(volumes == 60);
    REQUIRE(programs == 253);
    REQUIRE(samples > 0);
    REQUIRE(stereoPairs > 0);
    REQUIRE(checkedStereo);
    REQUIRE(file.getSize() == size);
    REQUIRE(file.getLastModificationTime() == modified);
    REQUIRE(FileScanner::computeFileHash(file) == hash);
}
