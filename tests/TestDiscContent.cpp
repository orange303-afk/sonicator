#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "core/disc/DiscContentIndexer.h"
#include "core/indexing/IndexingService.h"
#include "core/scanner/FileScanner.h"
#include "ui/MainComponent.h"
#include "ui/disc/DiscBrowserPanel.h"
#include "ui/library/FileListPanel.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <functional>
using namespace sonicator;
namespace {
void putLe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
void putBe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * (3 - i))) & 0xff);
}
size_t appendRecord(std::vector<unsigned char>& image, size_t at, const char* name,
                    uint32_t extent, uint32_t bytes, unsigned char flags)
{
    const bool dot = name[0] == '\0';
    const bool dotdot = (unsigned char) name[0] == 1;
    const size_t nameLength = (dot || dotdot) ? 1 : std::strlen(name);
    const size_t length = 33 + nameLength;
    const size_t padded = length + (length & 1);
    image[at] = (unsigned char) padded;
    putLe32(image, at + 2, extent); putBe32(image, at + 6, extent);
    putLe32(image, at + 10, bytes); putBe32(image, at + 14, bytes);
    image[at + 25] = flags;
    image[at + 32] = (unsigned char) nameLength;
    if (dot) image[at + 33] = 0;
    else if (dotdot) image[at + 33] = 1;
    else std::memcpy(image.data() + at + 33, name, nameLength);
    return at + padded;
}
void setPvd(std::vector<unsigned char>& image, unsigned rootExtent, unsigned rootBytes)
{
    auto* pvd = image.data() + 16 * 2048;
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    putLe32(image, 16 * 2048 + 80, 32); putBe32(image, 16 * 2048 + 84, 32);
    image[16 * 2048 + 128] = 0x00; image[16 * 2048 + 129] = 0x08;
    image[16 * 2048 + 130] = 0x08; image[16 * 2048 + 131] = 0x00;
    auto at = 16 * 2048 + 156;
    image[at] = 34;
    putLe32(image, at + 2, rootExtent); putBe32(image, at + 6, rootExtent);
    putLe32(image, at + 10, rootBytes); putBe32(image, at + 14, rootBytes);
    image[at + 25] = 0x02; image[at + 32] = 1;
}
juce::File writeIsoWithWaves(TestDirectory& t, const juce::String& name)
{
    juce::MemoryBlock audio;
    auto wave = t.wave("seed.wav", 440.0, 1, 200);
    REQUIRE(wave.loadFileAsData(audio));
    std::vector<unsigned char> image(32 * 2048, 0);
    setPvd(image, 20, 2048);
    auto* term = image.data() + 17 * 2048;
    term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    size_t at = 20 * 2048;
    at = appendRecord(image, at, "\0", 20, 2048, 0x02);
    at = appendRecord(image, at, "\1", 20, 2048, 0x02);
    at = appendRecord(image, at, "KICK.WAV;1", 21, (uint32_t) audio.getSize(), 0);
    at = appendRecord(image, at, "DRUMS", 22, 2048, 0x02);
    std::memcpy(image.data() + 21 * 2048, audio.getData(), audio.getSize());
    size_t sub = 22 * 2048;
    sub = appendRecord(image, sub, "\0", 22, 2048, 0x02);
    sub = appendRecord(image, sub, "\1", 20, 2048, 0x02);
    sub = appendRecord(image, sub, "SNARE.WAV;1", 23, (uint32_t) audio.getSize(), 0);
    sub = appendRecord(image, sub, "KICK.WAV;2", 24, (uint32_t) audio.getSize(), 0);
    std::memcpy(image.data() + 23 * 2048, audio.getData(), audio.getSize());
    std::memcpy(image.data() + 24 * 2048, audio.getData(), audio.getSize());
    auto file = t.dir.getChildFile(name);
    file.replaceWithData(image.data(), image.size());
    return file;
}
SampleRecord discRow(const juce::String& imagePath, const juce::String& internal,
                     const juce::String& entry = "10", const juce::String& parent = "1")
{
    SampleRecord r;
    r.filePath = imagePath + "::" + internal;
    r.fileName = juce::File(internal).getFileName();
    r.format = r.fileName.fromLastOccurrenceOf(".", false, false).toUpperCase();
    r.version = metadataOnlyVersion;
    r.compatibility = AssetCompatibility::supported;
    r.containerPath = imagePath;
    r.discEntry = entry;
    r.discParent = parent;
    r.discPath = internal;
    r.dateModified = 100; r.fileSize = 1000;
    return r;
}
}
TEST_CASE("Disc contents are searchable rows tied to their image", "[disc-content][database]")
{
    TestDirectory t;
    const auto imagePath = t.dir.getChildFile("library.iso").getFullPathName();
    Database db(t.dir.getChildFile("disc.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord image;
    image.filePath = imagePath; image.fileName = "library.iso"; image.format = "ISO";
    image.version = metadataOnlyVersion; image.compatibility = AssetCompatibility::supported;
    const auto imageId = repo.upsert(image);
    auto kick = discRow(imagePath, "/DRUMS/KICK.WAV", "21");
    auto snare = discRow(imagePath, "/DRUMS/SNARE.WAV", "22");
    repo.replaceDiscContents(imagePath, 4096, 1000, { kick, snare });

    auto state = repo.discIndexState(imagePath);
    REQUIRE(state.has_value());
    REQUIRE(state->entryCount == 2);
    REQUIRE(state->imageSize == 4096);

    LibraryFilter byName; byName.text = "kick";
    auto page = repo.query(byName, 10, 0);
    REQUIRE(page.total == 1);
    REQUIRE(page.samples[0].containerPath == imagePath);
    REQUIRE(page.samples[0].discEntry == "21");
    REQUIRE(page.samples[0].discPath == "/DRUMS/KICK.WAV");
    LibraryFilter byFolder; byFolder.text = "drums";
    REQUIRE(repo.query(byFolder, 10, 0).total == 2);

    const auto kickId = page.samples[0].id;
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    repo.setSampleTag(kickId, tag, true);
    repo.setStarred({ kickId }, true);

    // Reindexing an unchanged identity keeps ids, tags and stars.
    auto changed = discRow(imagePath, "/DRUMS/KICK.WAV", "21");
    changed.fileSize = 2000;
    repo.replaceDiscContents(imagePath, 4096, 1001, { changed });
    const auto stored = repo.findByPath(changed.filePath);
    REQUIRE(stored.has_value());
    REQUIRE(stored->id == kickId);
    REQUIRE(stored->fileSize == 2000);
    REQUIRE(stored->starred);
    REQUIRE(repo.tagsForSample(kickId).size() == 1);
    REQUIRE_FALSE(repo.findByPath(snare.filePath).has_value());

    // Ignoring the image hides its contents without deleting them.
    repo.ignorePath(imagePath, false);
    LibraryFilter visible; visible.hideEmpty = false;
    REQUIRE(repo.query(visible, 100, 0).total == 0); // the image row and its content are both hidden
    REQUIRE(repo.visibleIds({ kickId }).empty());
    repo.unignorePath(imagePath);
    REQUIRE(repo.visibleIds({ kickId }) == std::vector<int64_t>{ kickId });

    repo.removeByPaths({ imagePath });
    REQUIRE_FALSE(repo.findByPath(changed.filePath).has_value());
    REQUIRE_FALSE(repo.discIndexState(imagePath).has_value());
    REQUIRE_FALSE(repo.findByPath(imagePath).has_value());
    REQUIRE(imageId > 0);
}
TEST_CASE("A missing image marks its contents missing instead of deleting tags", "[disc-content][database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("prune.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto gonePath = t.dir.getChildFile("gone.iso").getFullPathName();
    const auto livePath = t.dir.getChildFile("live.iso").getFullPathName();
    SampleRecord gone, live;
    gone.filePath = gonePath; gone.fileName = "gone.iso"; gone.format = "ISO";
    live.filePath = livePath; live.fileName = "live.iso"; live.format = "ISO";
    const auto goneId = repo.upsert(gone);
    repo.upsert(live);
    repo.replaceDiscContents(gonePath, 1, 2, { discRow(gonePath, "/GONE.WAV") });
    repo.replaceDiscContents(livePath, 1, 2, { discRow(livePath, "/LIVE.WAV") });
    const auto virtualId = repo.findByPath(gonePath + "::/GONE.WAV")->id;
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    repo.setSampleTag(virtualId, tag, true);
    repo.setStarred({ virtualId }, true);
    repo.setMissing({ goneId });
    REQUIRE(repo.pruneDiscContents() == 0); // unplugged/deleted images keep their catalog rows
    auto virtualRow = repo.findByPath(gonePath + "::/GONE.WAV");
    REQUIRE(virtualRow.has_value());
    REQUIRE(virtualRow->missing);
    REQUIRE(repo.tagsForSample(virtualId).size() == 1);
    REQUIRE(virtualRow->starred);
    REQUIRE(repo.discIndexState(gonePath).has_value());
    REQUIRE_FALSE(repo.findByPath(livePath + "::/LIVE.WAV")->missing);

    // The image returns: the same rows become current again.
    auto returned = gone; returned.missing = false;
    repo.upsert(returned);
    repo.pruneDiscContents();
    REQUIRE_FALSE(repo.findByPath(gonePath + "::/GONE.WAV")->missing);
    REQUIRE(repo.tagsForSample(virtualId).size() == 1);
}

TEST_CASE("An indexed ISO contributes searchable rows without extraction", "[disc-content][indexing]")
{
    TestDirectory t;
    const auto iso = writeIsoWithWaves(t, "library.iso");
    Database db(t.dir.getChildFile("index.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(t.dir.getFullPathName());
    IndexingService indexer(repo);
    REQUIRE(indexer.start({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(30000));
    const auto progress = indexer.progress();
    INFO(progress.discMessage);
    REQUIRE(progress.discImages == 1);
    REQUIRE(progress.discEntries == 3);
    REQUIRE(progress.discFailed == 0);

    auto image = repo.findByPath(iso.getFullPathName());
    REQUIRE(image.has_value());
    REQUIRE(image->format == "ISO");
    const auto kickPath = iso.getFullPathName() + "::/KICK.WAV";
    auto kick = repo.findByPath(kickPath);
    INFO(kickPath);
    REQUIRE(kick.has_value());
    REQUIRE(kick->containerPath == iso.getFullPathName());
    REQUIRE(kick->discEntry.isNotEmpty());
    REQUIRE(kick->discPath == "/KICK.WAV");
    REQUIRE(kick->version == metadataOnlyVersion);
    REQUIRE(kick->duration == 0);
    REQUIRE(kick->fileHash.isEmpty());
    REQUIRE(kick->isDiscEntry());
    auto snare = repo.findByPath(iso.getFullPathName() + "::/DRUMS/SNARE.WAV");
    REQUIRE(snare.has_value());
    REQUIRE(snare->discPath == "/DRUMS/SNARE.WAV");

    LibraryFilter byName; byName.text = "snare";
    auto found = repo.query(byName, 10, 0);
    REQUIRE(found.total == 1);
    REQUIRE(found.samples[0].id == snare->id);
    LibraryFilter byKick; byKick.text = "kick";
    REQUIRE(repo.query(byKick, 10, 0).total == 2); // same name, different internal folders
    LibraryFilter byInternalPath; byInternalPath.text = "drums";
    REQUIRE(repo.query(byInternalPath, 10, 0).total == 2);
    LibraryFilter visible; visible.hideEmpty = true;
    REQUIRE(repo.query(visible, 100, 0).total >= 3); // image + two metadata-only contents
    for (auto id : repo.samplesNeedingSmartAnalysis(0, 100))
        REQUIRE(repo.findById(id)->containerPath.isEmpty()); // virtual rows are never Smart inputs

    // A second scan sees an unchanged image and keeps the rows and their ids.
    const auto kickId = kick->id;
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    repo.setSampleTag(kickId, tag, true);
    REQUIRE(indexer.start({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(30000));
    REQUIRE(indexer.progress().discSkipped == 1);
    REQUIRE(repo.findByPath(kickPath)->id == kickId);
    REQUIRE(repo.tagsForSample(kickId).size() == 1);

    // A modified image is walked again; stable paths keep their ids and tags.
    juce::Thread::sleep(1100);
    iso.setLastModificationTime(juce::Time::getCurrentTime());
    REQUIRE(indexer.start({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(30000));
    REQUIRE(indexer.progress().discImages == 1);
    REQUIRE(repo.findByPath(kickPath)->id == kickId);
    REQUIRE(repo.tagsForSample(kickId).size() == 1);

    // Removing the image row also removes its synthetic contents.
    repo.removeByPaths({ iso.getFullPathName() });
    REQUIRE_FALSE(repo.findByPath(kickPath).has_value());
    REQUIRE_FALSE(repo.findByPath(iso.getFullPathName() + "::/DRUMS/SNARE.WAV").has_value());
    REQUIRE(iso.existsAsFile());
}

TEST_CASE("Deleting a disc image keeps its contents marked missing for a later return", "[disc-content][indexing]")
{
    TestDirectory t;
    const auto iso = writeIsoWithWaves(t, "gone.iso");
    Database db(t.dir.getChildFile("prune-index.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(t.dir.getFullPathName());
    IndexingService indexer(repo);
    REQUIRE(indexer.start({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(30000));
    const auto virtualPath = iso.getFullPathName() + "::/KICK.WAV";
    REQUIRE(repo.findByPath(virtualPath).has_value());
    REQUIRE(iso.deleteFile());
    REQUIRE(indexer.start({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(30000));
    REQUIRE(repo.findByPath(iso.getFullPathName())->missing);
    REQUIRE(repo.findByPath(virtualPath)->missing); // tags survive an unplugged or moved image
    REQUIRE(repo.discIndexState(iso.getFullPathName()).has_value());
}

TEST_CASE("A disc content row previews and opens the image at its entry", "[disc-content][ui]")
{
    TestDirectory t;
    const auto iso = writeIsoWithWaves(t, "ui.iso");
    const auto dbPath = t.dir.getChildFile("ui.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        repo.addRoot(t.dir.getFullPathName());
        IndexingService indexer(repo);
        REQUIRE(indexer.start({t.dir.getFullPathName()}));
        REQUIRE(indexer.waitUntilFinished(30000));
    }
    const auto pumpUntil = [](const std::function<bool()>& ready, int timeoutMs)
    {
        const auto start = juce::Time::currentTimeMillis();
        while (! ready() && juce::Time::currentTimeMillis() - start < timeoutMs)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        return ready();
    };
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"));
    app.setSize(1200, 800);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    auto* preview = dynamic_cast<PreviewPanel*>(app.findChildWithID("preview"));
    REQUIRE(library != nullptr); REQUIRE(preview != nullptr);
    app.searchLibrary("snare");
    REQUIRE(library->getNumRows() == 1);
    library->selectRow(0);
    REQUIRE(pumpUntil([preview] { return preview->waveformLength() > 0.0; }, 10000));
    auto* browser = dynamic_cast<DiscBrowserPanel*>(app.findChildWithID("discBrowser"));
    REQUIRE(browser != nullptr);
    REQUIRE_FALSE(browser->isVisible()); // selection alone previews without leaving the library

    // Enter opens the image on the entry's folder.
    REQUIRE(app.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    REQUIRE(pumpUntil([browser] { return browser->isVisible(); }, 10000));
    auto* status = dynamic_cast<juce::Label*>(browser->findChildWithID("discStatus"));
    REQUIRE(status != nullptr);
    const bool ready = pumpUntil([status] { return status->getText().startsWith("Ready: SNARE.WAV"); }, 10000);
    INFO("status=" << status->getText() << " rows=" << browser->getNumRows());
    REQUIRE(ready);
    REQUIRE(preview->waveformLength() > 0.0);
}

TEST_CASE("Disc content indexing is cancellable and skips BIN covered by a CUE", "[disc-content]")
{
    TestDirectory t;
    const auto iso = writeIsoWithWaves(t, "cancel.iso");
    Database db(t.dir.getChildFile("cancel.sqlite")); SampleRepository repo(db); repo.createTables();
    const auto summary = indexDiscContents(repo, { iso }, [] { return true; });
    REQUIRE(summary.imagesTried == 0);
    REQUIRE(summary.entries == 0);
    REQUIRE_FALSE(repo.discIndexState(iso.getFullPathName()).has_value());

    auto bin = t.dir.getChildFile("disk.bin");
    bin.replaceWithData("data", 4);
    REQUIRE(isDiscContentCandidate(bin));
    t.dir.getChildFile("disk.cue").replaceWithText("FILE \"disk.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
    REQUIRE_FALSE(isDiscContentCandidate(bin));
    REQUIRE(isDiscContentCandidate(t.dir.getChildFile("disk.cue")));
    REQUIRE_FALSE(isDiscContentCandidate(t.dir.getChildFile("volume.mdx")));
    auto img = t.dir.getChildFile("image-disk.img");
    img.replaceWithData("data", 4);
    REQUIRE(isDiscContentCandidate(img));
    t.dir.getChildFile("image-disk.cue").replaceWithText("FILE \"image-disk.img\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    REQUIRE_FALSE(isDiscContentCandidate(img));
}

TEST_CASE("Removing a folder drops disc index state so contents return after re-add", "[disc-content][remove]")
{
    TestDirectory t;
    const auto imagePath = t.dir.getChildFile("State/lib.iso").getFullPathName();
    Database db(t.dir.getChildFile("state.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord image;
    image.filePath = imagePath; image.fileName = "lib.iso"; image.format = "ISO";
    repo.upsert(image);
    repo.replaceDiscContents(imagePath, 10, 20, { discRow(imagePath, "/KICK.WAV") });
    REQUIRE(repo.discIndexState(imagePath).has_value());
    const auto removed = repo.removeFolder(t.dir.getChildFile("State").getFullPathName());
    REQUIRE(removed.rowsRemoved == 2);
    REQUIRE_FALSE(repo.discIndexState(imagePath).has_value());
    REQUIRE_FALSE(repo.findByPath(imagePath + "::/KICK.WAV").has_value());
}

// Opt-in real-image check:
//   SONICATOR_DISC_IMAGE=/path/to/image.iso build/tests/SonicatorTests "[.disc-real-content]"
TEST_CASE("A real image indexes content and previews a decoded sample", "[.disc-real-content]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_IMAGE", {});
    if (path.isEmpty()) SKIP("SONICATOR_DISC_IMAGE is not set");
    const juce::File image(path);
    REQUIRE(image.existsAsFile());
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("real.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        SampleRecord record;
        record.filePath = image.getFullPathName(); record.fileName = image.getFileName();
        record.format = "AKAI"; record.fileSize = image.getSize();
        record.dateModified = image.getLastModificationTime().toMilliseconds();
        record.version = metadataOnlyVersion; record.compatibility = AssetCompatibility::supported;
        repo.upsert(record);
        const auto summary = indexDiscContents(repo, { image }, {});
        INFO(summary.lastError);
        REQUIRE(summary.imagesIndexed == 1);
        REQUIRE(summary.entries > 0);
        REQUIRE(repo.discIndexState(image.getFullPathName()).has_value());
    }
    SampleRecord virtualRow;
    {
        Database db(dbPath); SampleRepository repo(db);
        for (const auto& row : repo.page(100000))
            if (row.isDiscEntry()) { virtualRow = row; break; }
        REQUIRE(virtualRow.filePath.isNotEmpty());
        REQUIRE(virtualRow.containerPath == image.getFullPathName());
        REQUIRE(virtualRow.version == metadataOnlyVersion);
    }
    AppSettings settings;
    settings.lastSamplePath = virtualRow.filePath;
    MainComponent app(dbPath, false, settings, t.dir.getChildFile("settings.json"));
    app.setSize(1200, 800);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    auto* preview = dynamic_cast<PreviewPanel*>(app.findChildWithID("preview"));
    REQUIRE(library != nullptr); REQUIRE(preview != nullptr);
    REQUIRE(library->getSelectedSamples().size() == 1);
    REQUIRE(app.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    const auto start = juce::Time::currentTimeMillis();
    while (preview->waveformLength() <= 0.0 && juce::Time::currentTimeMillis() - start < 20000)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    INFO(virtualRow.filePath);
    REQUIRE(preview->waveformLength() > 0.0);
}

TEST_CASE("Relocating a root moves disc containers and index state", "[disc-content][relocate]")
{
    TestDirectory t;
    const auto oldRoot = t.dir.getChildFile("Old"); oldRoot.createDirectory();
    const auto newRoot = t.dir.getChildFile("New"); newRoot.createDirectory();
    const auto oldImage = oldRoot.getChildFile("lib.iso").getFullPathName();
    const auto newImage = newRoot.getChildFile("lib.iso").getFullPathName();
    Database db(t.dir.getChildFile("relocate.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(oldRoot.getFullPathName());
    SampleRecord image;
    image.filePath = oldImage; image.fileName = "lib.iso"; image.format = "ISO";
    repo.upsert(image);
    repo.replaceDiscContents(oldImage, 10, 20, { discRow(oldImage, "/KICK.WAV") });
    const auto result = repo.relocateRoots({ { oldRoot.getFullPathName(), newRoot.getFullPathName() } });
    REQUIRE(result.movedFiles >= 2);
    auto moved = repo.findByPath(newImage + "::/KICK.WAV");
    REQUIRE(moved.has_value());
    REQUIRE(moved->containerPath == newImage);
    REQUIRE(moved->discPath == "/KICK.WAV");
    REQUIRE_FALSE(repo.findByPath(oldImage + "::/KICK.WAV").has_value());
    REQUIRE(repo.discIndexState(newImage).has_value());
    REQUIRE_FALSE(repo.discIndexState(oldImage).has_value());
}

TEST_CASE("A partial disc listing keeps earlier rows and tags", "[disc-content][database]")
{
    TestDirectory t;
    const auto imagePath = t.dir.getChildFile("partial.iso").getFullPathName();
    Database db(t.dir.getChildFile("partial.sqlite")); SampleRepository repo(db); repo.createTables();
    auto first = discRow(imagePath, "/A.WAV", "1");
    auto second = discRow(imagePath, "/B.WAV", "2");
    repo.replaceDiscContents(imagePath, 10, 20, { first, second });
    const auto secondId = repo.findByPath(second.filePath)->id;
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    repo.setSampleTag(secondId, tag, true);
    repo.replaceDiscContents(imagePath, 10, 21, { first }, false); // partial listing: merge only
    REQUIRE(repo.findByPath(second.filePath).has_value());
    REQUIRE(repo.tagsForSample(secondId).size() == 1);
    repo.invalidateDiscState(imagePath);
    REQUIRE_FALSE(repo.discIndexState(imagePath).has_value());
    repo.replaceDiscContents(imagePath, 10, 22, { first }, true); // complete listing: B is gone
    REQUIRE_FALSE(repo.findByPath(second.filePath).has_value());
}
TEST_CASE("A CUE image identity includes its data file", "[disc-content]")
{
    TestDirectory t;
    const auto bin = t.dir.getChildFile("disk.bin");
    bin.replaceWithData("data", 4);
    const auto cue = t.dir.getChildFile("disk.cue");
    cue.replaceWithText("FILE \"disk.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
    const auto first = discImageStamp(cue);
    REQUIRE(first.ok);
    REQUIRE(first.size == 4);
    juce::Thread::sleep(1100);
    bin.setLastModificationTime(juce::Time::getCurrentTime());
    const auto changedBin = discImageStamp(cue);
    REQUIRE(changedBin.modified != first.modified);
    juce::Thread::sleep(1100);
    cue.replaceWithText("FILE \"disk.bin\" BINARY\n  TRACK 01 MODE1/2048\n  REM edited\n    INDEX 01 00:00:00\n");
    const auto changedCue = discImageStamp(cue);
    REQUIRE(changedCue.modified != changedBin.modified);
    const auto binStamp = discImageStamp(bin);
    REQUIRE(binStamp.ok);
    REQUIRE(binStamp.size == 4);
}
TEST_CASE("Smart folder and selected scopes exclude disc contents", "[disc-content][smart]")
{
    TestDirectory t;
    const auto imagePath = t.dir.getChildFile("smart.iso").getFullPathName();
    Database db(t.dir.getChildFile("smart.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord image; image.filePath = imagePath; image.fileName = "smart.iso"; image.format = "ISO";
    const auto imageId = repo.upsert(image);
    const auto virtualId = repo.upsert(discRow(imagePath, "/KICK.WAV"));
    const auto normal = t.wave("normal.wav");
    const auto normalId = repo.upsert(BasicAnalyzer().analyze(normal));
    REQUIRE(repo.idsInFolder(t.dir.getFullPathName(), false) == std::vector<int64_t>{ imageId, normalId });
    REQUIRE(repo.countInFolder(t.dir.getFullPathName(), false) == 2);
    REQUIRE(repo.idsInFolder(t.dir.getFullPathName(), true).size() == 3);
    REQUIRE(repo.nonDiscIds({ virtualId, normalId }) == std::vector<int64_t>{ normalId });
}
TEST_CASE("New-only scans leave disc contents untouched", "[disc-content][indexing]")
{
    TestDirectory t;
    const auto iso = writeIsoWithWaves(t, "newonly.iso");
    Database db(t.dir.getChildFile("newonly.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(t.dir.getFullPathName());
    IndexingService indexer(repo);
    REQUIRE(indexer.start({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(30000));
    const auto state = repo.discIndexState(iso.getFullPathName());
    REQUIRE(state.has_value());
    juce::Thread::sleep(1100);
    iso.setLastModificationTime(juce::Time::getCurrentTime());
    REQUIRE(indexer.startNewOnly({t.dir.getFullPathName()}));
    REQUIRE(indexer.waitUntilFinished(30000));
    REQUIRE(indexer.progress().discImages == 0); // additive scan does not rewrite contents
    REQUIRE(repo.discIndexState(iso.getFullPathName())->imageModified == state->imageModified);
}

TEST_CASE("A newer library selection wins over a hidden disc preview", "[disc-content][ui]")
{
    TestDirectory t;
    writeIsoWithWaves(t, "race.iso");
    const auto dbPath = t.dir.getChildFile("race.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        repo.addRoot(t.dir.getFullPathName());
        IndexingService indexer(repo);
        REQUIRE(indexer.start({t.dir.getFullPathName()}));
        REQUIRE(indexer.waitUntilFinished(30000));
    }
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("race-settings.json"));
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    auto* preview = dynamic_cast<PreviewPanel*>(app.findChildWithID("preview"));
    REQUIRE(library != nullptr); REQUIRE(preview != nullptr);
    app.searchLibrary("snare");
    REQUIRE(library->getNumRows() == 1);
    library->selectRow(0); // starts the hidden disc preview
    app.searchLibrary("seed");
    REQUIRE(library->getNumRows() == 1);
    library->selectRow(0); // user picks an ordinary WAV before/after the disc decode finishes
    const auto start = juce::Time::currentTimeMillis();
    while (preview->waveformLength() <= 0.0 && juce::Time::currentTimeMillis() - start < 10000)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1000);
    REQUIRE_FALSE(preview->isDiscPreview());
    REQUIRE(preview->waveformLength() > 0.0);
}
