#include <catch2/catch_test_macros.hpp>
#include "ui/preview/PreviewPanel.h"
#include "ui/library/FolderTree.h"
#include "TestHelpers.h"
using namespace sonicator;

TEST_CASE("Ordinary audio has no container source rows", "[ui][polish]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    auto* list = dynamic_cast<juce::ListBox*>(preview.findChildWithID("containerSources"));
    REQUIRE(list != nullptr);
    REQUIRE(preview.loadFile(t.wave(), false));
    REQUIRE(list->getListBoxModel()->getNumRows() == 0);
}

TEST_CASE("Images and their virtual directories expand in the folder tree", "[ui][polish]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("tree.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(t.dir.getFullPathName());
    const auto image = t.dir.getChildFile("bank.iso").getFullPathName();
    SampleRecord record; record.filePath = image; record.fileName = "bank.iso"; record.format = "ISO";
    repo.upsert(record);
    FolderTree folders;
    folders.refresh(repo);
    REQUIRE(folders.selectPath(image));
    auto* tree = dynamic_cast<juce::TreeView*>(folders.getChildComponent(0));
    REQUIRE(tree != nullptr);
    auto* imageItem = tree->getSelectedItem(0);
    REQUIRE(imageItem != nullptr);
    REQUIRE(imageItem->mightContainSubItems());
    DiscEntry sub; sub.id = {2}; sub.parent = {1}; sub.kind = DiscEntryKind::directory; sub.displayName = "Drums";
    DiscEntry nested; nested.id = {3}; nested.parent = {2}; nested.kind = DiscEntryKind::directory; nested.displayName = "Kicks";
    folders.setDiscFolders(image, {1}, {sub, nested});
    REQUIRE(imageItem->isOpen());
    REQUIRE(imageItem->getNumSubItems() == 1);
    auto* drums = imageItem->getSubItem(0);
    drums->setOpen(true);
    REQUIRE(drums->getNumSubItems() == 1);
    DiscEntryId selected;
    folders.onDiscFolder = [&](const juce::String& path, DiscEntryId id) { REQUIRE(path == image); selected = id; };
    drums->getSubItem(0)->setSelected(true, true);
    REQUIRE(selected == nested.id);
}
