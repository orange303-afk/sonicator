#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <iostream>
#include "core/ableton/AbletonPacks.h"
#include "core/audio/AudioEngine.h"
#include "core/database/LibraryQuery.h"
#include "core/database/SampleRepository.h"
#include "core/indexing/IndexingService.h"
#include "core/scanner/FileScanner.h"
#include "core/settings/AppSettings.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace {
void be32(std::vector<unsigned char>& b, juce::uint32 x)
{
    for (int i = 3; i >= 0; --i) b.push_back((unsigned char) ((x >> (8 * i)) & 0xff));
}
void ascii(std::vector<unsigned char>& b, const char* text) { b.insert(b.end(), text, text + 4); }
juce::File writeAifc(TestDirectory& t, const juce::String& name, const char* codec)
{
    std::vector<unsigned char> comm;
    comm.push_back(0); comm.push_back(2);          // stereo
    be32(comm, 1000);                              // frames
    comm.push_back(0); comm.push_back(16);         // bits
    const unsigned char rate[10] { 0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0 };
    comm.insert(comm.end(), rate, rate + 10);
    ascii(comm, codec);
    std::vector<unsigned char> body;
    ascii(body, "FVER"); be32(body, 4); be32(body, 0xa2805140);
    ascii(body, "COMM"); be32(body, (juce::uint32) comm.size());
    body.insert(body.end(), comm.begin(), comm.end());
    std::vector<unsigned char> file;
    ascii(file, "FORM"); be32(file, (juce::uint32) body.size() + 4); ascii(file, "AIFC");
    file.insert(file.end(), body.begin(), body.end());
    auto f = t.dir.getChildFile(name);
    f.replaceWithData(file.data(), file.size());
    return f;
}
juce::File firstPreview(const juce::File& root)
{
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.ogg", juce::File::findFiles,
                                                          juce::File::FollowSymlinks::no))
        if (entry.getFile().getFullPathName().containsIgnoreCase("/Ableton Folder Info/Previews/"))
            return entry.getFile();
    return {};
}
juce::File firstAif(const juce::File& root, bool protectedSample)
{
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.aif;*.aiff", juce::File::findFiles,
                                                          juce::File::FollowSymlinks::no))
        if (isAbletonProtectedSample(entry.getFile()) == protectedSample)
            return entry.getFile();
    return {};
}
}
TEST_CASE("OGG is scanned while Ableton protected AIFC samples are skipped", "[ableton][scanner]")
{
    TestDirectory t;
    const auto preview = t.dir.getChildFile("Song/Ableton Folder Info/Previews/Clips/Wicked-95-A.alc.ogg");
    REQUIRE(FileScanner::isSupportedFormat(preview)); // extension is enough; no file is decoded here
    const auto standard = writeAifc(t, "standard.aif", "NONE");
    const auto floating = writeAifc(t, "float.aif", "fl32");
    const auto protectedSample = writeAifc(t, "protected.aif", "able");
    REQUIRE(FileScanner::isSupportedFormat(standard));
    REQUIRE(FileScanner::isSupportedFormat(floating));
    REQUIRE(isAbletonProtectedSample(protectedSample));
    REQUIRE_FALSE(FileScanner::isSupportedFormat(protectedSample));
    REQUIRE(FileScanner::scanDirectory(t.dir).size() == 2);
}
TEST_CASE("Ableton preview paths name their source and kind", "[ableton]")
{
    const auto clip = abletonPreviewInfo(juce::File(
        "/Packs/Designer Drums/Ableton Folder Info/Previews/Clips/Wicked-95-A.alc.ogg"));
    REQUIRE(clip.isPreview);
    REQUIRE(clip.source == "Wicked-95-A");
    REQUIRE(clip.kind == "Clip");
    const auto rack = abletonPreviewInfo(juce::File(
        "/Packs/Drum Machines/Ableton Folder Info/Previews/Drums/Kit-707 Classic.adg.ogg"));
    REQUIRE(rack.isPreview);
    REQUIRE(rack.source == "Kit-707 Classic");
    REQUIRE(rack.kind == "Rack");
    const auto device = abletonPreviewInfo(juce::File(
        "/Packs/Granulator III/Ableton Folder Info/Previews/Device Templates/Vanilla Cloud.adv.ogg"));
    REQUIRE(device.isPreview);
    REQUIRE(device.source == "Vanilla Cloud");
    REQUIRE(device.kind == "Device");
    REQUIRE_FALSE(abletonPreviewInfo(juce::File("/Music/loops/ordinary.ogg")).isPreview);
    REQUIRE_FALSE(abletonPreviewInfo(juce::File(
        "/Packs/Song/Ableton Folder Info/Previews/Clips/notes.txt")).isPreview);
}
TEST_CASE("Ableton Factory Packs registration persists once", "[ableton][settings]")
{
    TestDirectory t; Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SettingsStore settings(t.dir.getChildFile("prefs.json"));
    const auto root = t.dir.getChildFile("Ableton/Factory Packs");
    REQUIRE_FALSE(registerAbletonPacks(repo, settings, root));
    root.createDirectory();
    REQUIRE(registerAbletonPacks(repo, settings, root));
    REQUIRE(settings.load().abletonPacksAdded);
    REQUIRE(repo.roots().contains(root.getFullPathName()));
    Database other(t.dir.getChildFile("other.sqlite")); SampleRepository empty(other); empty.createTables();
    REQUIRE_FALSE(registerAbletonPacks(empty, settings, root));
    REQUIRE(empty.roots().isEmpty());
}
TEST_CASE("Installed Ableton previews decode and are labelled", "[ableton][real]")
{
    const auto root = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Ableton/Factory Packs");
    if (!root.isDirectory()) SKIP("Ableton Factory Packs are not installed");
    const auto file = firstPreview(root);
    if (file == juce::File()) SKIP("No Ableton OGG previews found");
    TestDirectory t; AudioEngine engine(false); PreviewPanel panel(engine, t.dir.getChildFile("cache"));
    INFO(file.getFullPathName());
    REQUIRE(panel.loadFile(file, false));
    REQUIRE(engine.length() > 0);
    REQUIRE_FALSE(engine.isSamplerInstrumentMode());
    auto* status = dynamic_cast<juce::Label*>(panel.findChildWithID("selectionStatus"));
    REQUIRE(status != nullptr);
    REQUIRE(status->getText().contains("Ableton Preview"));
    REQUIRE(status->getText().containsIgnoreCase("preview"));
}
TEST_CASE("Ableton previews index into the general search", "[ableton][indexing][real]")
{
    const auto root = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Ableton/Factory Packs");
    if (!root.isDirectory()) SKIP("Ableton Factory Packs are not installed");
    const auto source = firstPreview(root);
    if (source == juce::File()) SKIP("No Ableton OGG previews found");
    TestDirectory t;
    const auto copy = t.dir.getChildFile("Imported/Wicked-95-A.alc.ogg");
    copy.getParentDirectory().createDirectory();
    REQUIRE(source.copyFileTo(copy));
    Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService indexer(repo);
    REQUIRE(indexer.start({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(20000));
    auto record = repo.findByPath(copy.getFullPathName());
    REQUIRE(record.has_value());
    INFO(record->error);
    REQUIRE(record->error.isEmpty());
    REQUIRE(record->duration > 0);
    LibraryFilter filter; filter.text = "wicked";
    REQUIRE(repo.query(filter, 10, 0).total == 1);
    LibraryFilter byFormat; byFormat.text = "ext:ogg";
    REQUIRE(repo.query(byFormat, 10, 0).total == 1);
}
TEST_CASE("Installed Ableton packs scan playable audio without protected samples", "[ableton][real]")
{
    const auto root = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Ableton/Factory Packs");
    if (!root.isDirectory()) SKIP("Ableton Factory Packs are not installed");
    const auto files = FileScanner::scanDirectory(root);
    int ogg = 0, aif = 0;
    for (const auto& item : files)
    {
        if (item.file.hasFileExtension("ogg")) ++ogg;
        if (item.file.hasFileExtension("aif;aiff"))
        {
            ++aif;
            REQUIRE_FALSE(isAbletonProtectedSample(item.file)); // protected content never reaches the catalog
        }
    }
    std::cout << "Ableton Factory Packs: supported files=" << files.size()
              << " ogg=" << ogg << " aif/aiff=" << aif << "\n";
    REQUIRE(ogg > 0);
    REQUIRE(aif > 0);
}
