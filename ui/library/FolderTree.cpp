#include "FolderTree.h"
#include "ui/shell/Theme.h"
namespace sonicator {
class FolderTree::Item final : public juce::TreeViewItem
{
public:
    Item(FolderTree& o,juce::String p,bool top = false) : owner(o),path(std::move(p)),all(top) {}
    bool mightContainSubItems() override
    {
        if (all || path.isEmpty()) return ! owner.roots.isEmpty();
        if (owner.images.count(path)) return true;
        return ! owner.children[path].empty();
    }
    juce::String getUniqueName() const override
    {
        return all ? "All samples" : (path.isEmpty() ? juce::String("Favorites") : path);
    }
    int getItemHeight() const override { return 26; }
    void paintItem(juce::Graphics& g,int w,int h) override
    {
        if (! all && path.isEmpty()) return; // hidden container root
        if (isSelected()) { g.setColour(theme::selection); g.fillAll(); }
        const bool virtualFolder = owner.virtualFolders.count(path) != 0;
        const bool image = owner.images.count(path) != 0;
        const bool missing = path.isNotEmpty() && ! virtualFolder && ! image && ! juce::File(path).isDirectory();
        const bool ignored = path.isNotEmpty() && isPathIgnored(path, owner.ignoredEntries);
        g.setColour(owner.failedImages.count(path)
            ? (theme::isDark ? juce::Colour(0xffff8b8b) : juce::Colour(0xffb3261e))
            : (ignored ? theme::muted : (missing ? theme::muted : theme::text))); g.setFont(13.0f);
        const auto baseName = virtualFolder ? owner.virtualFolders.at(path).name
            : (image ? "[IMG] " : "") + juce::File(path).getFileName();
        const auto name = all ? juce::String("All samples")
            : baseName + (ignored && owner.showIgnored ? " [ignored]" : juce::String());
        g.drawText(name, 3,0,w-6,h,juce::Justification::centredLeft,true);
    }
    void itemSelectionChanged(bool selected) override
    {
        if (! selected || owner.restoring) return;
        if (auto found = owner.virtualFolders.find(path); found != owner.virtualFolders.end())
        {
            if (owner.onDiscFolder) owner.onDiscFolder(found->second.image, found->second.id);
        }
        else if (owner.onFolder && (all || path.isNotEmpty())) owner.onFolder(all ? juce::String() : path);
    }
    void itemClicked(const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || owner.onContextMenu == nullptr) return;
        if (owner.virtualFolders.count(path)) return;
        if (! all && path.isEmpty()) return;
        if (! isSelected()) setSelected(true, true);
        owner.onContextMenu(all ? juce::String() : path, all, e.getScreenPosition());
    }
    void itemOpennessChanged(bool open) override
    {
        if (! open || getNumSubItems() != 0) return;
        if (owner.images.count(path) && owner.children[path].empty() && ! owner.restoring && owner.onFolder)
            owner.onFolder(path);
        if (all || path.isEmpty()) { for (const auto& p : owner.roots) addSubItem(new Item(owner,p)); }
        else { for (const auto& p : owner.children[path]) addSubItem(new Item(owner,p)); }
        // JUCE notifies before this callback runs, so the newly added rows need another update.
        treeHasChanged();
    }
private:
    FolderTree& owner;
    juce::String path;
    bool all;
};
FolderTree::FolderTree()
{
    addAndMakeVisible(tree); tree.setRootItemVisible(true); tree.setMultiSelectEnabled(false);
    tree.setColour(juce::TreeView::backgroundColourId,juce::Colours::transparentBlack); tree.setDefaultOpenness(false);
    tree.setIndentSize(14); tree.setWantsKeyboardFocus(true);
}
FolderTree::~FolderTree() { tree.setRootItem(nullptr); }
void FolderTree::resized() { tree.setBounds(getLocalBounds()); }
void FolderTree::clearSelection()
{
    if (auto* selected = tree.getSelectedItem(0)) selected->setSelected(false, true);
}
bool FolderTree::selectPath(const juce::String& path)
{
    if (path.isEmpty()) return false;
    auto* item = tree.getRootItem();
    if (item == nullptr) return false;
    if (item->getUniqueName() == path) { item->setSelected(true, true); return true; }
    item->setOpen(true);
    return selectPath(*item, path);
}
bool FolderTree::selectPath(juce::TreeViewItem& parent, const juce::String& path)
{
    for (int i = 0; i < parent.getNumSubItems(); ++i)
    {
        auto* child = parent.getSubItem(i);
        if (child == nullptr) continue;
        const auto name = child->getUniqueName();
        if (name == path)
        {
            child->setSelected(true, true);
            tree.scrollToKeepItemVisible(child);
            return true;
        }
        if (name == "All samples" || name == "Favorites") continue;
        if (juce::File(path).isAChildOf(juce::File(name)))
        {
            child->setOpen(true);
            if (selectPath(*child, path)) return true;
        }
    }
    return false;
}
void FolderTree::lookAndFeelChanged()
{
    tree.setColour(juce::TreeView::backgroundColourId,juce::Colours::transparentBlack);
    tree.repaint();
}
void FolderTree::refresh(SampleRepository& repo)
{
    refresh(repo, repo.roots(), true, showIgnored);
}
void FolderTree::refresh(SampleRepository& repo, const juce::StringArray& rootPaths, bool showAllSamples,
                         bool revealIgnored)
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoring,true);
    this->showIgnored = revealIgnored;
    ignoredEntries = repo.ignoredPaths();
    auto openness = tree.getOpennessState(false);
    tree.setRootItem(nullptr); root.reset(); children.clear(); images.clear(); failedImages.clear(); virtualFolders.clear(); roots = rootPaths;
    if (showAllSamples)
        for (int i = roots.size(); --i >= 0;)
            if (isPathIgnored(roots[i], ignoredEntries)) roots.remove(i);
    tree.setRootItemVisible(showAllSamples);
    std::set<juce::String> visitedFolders;
    int64_t last = 0;
    for (;;)
    {
        auto rows = repo.pathsAfterId(last,5000);
        if (rows.empty()) break;
        last = rows.back().id;
        std::vector<int64_t> imageIds;
        for (const auto& row : rows)
        {
            if (! revealIgnored && isPathIgnored(row.path, ignoredEntries)) continue;
            auto folder = juce::File(row.path).getParentDirectory();
            if (juce::File(row.path).hasFileExtension("iso;bin;mdx;cue;akai;img"))
            {
                images.insert(row.path);
                imageIds.push_back(row.id);
                children[folder.getFullPathName()].insert(row.path);
            }
            // Most files share folders. Build each ancestor chain once, not once per audio file.
            if (! visitedFolders.insert(folder.getFullPathName()).second) continue;
            for (const auto& r : roots)
            {
                const juce::File boundary(r);
                if (folder != boundary && ! folder.isAChildOf(boundary)) continue;
                while (folder != boundary)
                {
                    auto parent = folder.getParentDirectory();
                    children[parent.getFullPathName()].insert(folder.getFullPathName()); folder = parent;
                }
                break;
            }
        }
        for (const auto& image : repo.recordsByIds(imageIds))
            if (isIncompatible(image.compatibility) || image.error.isNotEmpty()) failedImages.insert(image.filePath);
    }
    root = std::make_unique<Item>(*this,juce::String(),showAllSamples);
    tree.setRootItem(root.get()); root->setOpen(true);
    if (openness) tree.restoreOpennessState(*openness,true);
}
void FolderTree::setDiscFolders(const juce::String& image, DiscEntryId rootId, const std::vector<DiscEntry>& entries)
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoring, true);
    const auto key = [&image, rootId](DiscEntryId id)
    { return id == rootId ? image : image + "::" + juce::String((juce::int64) id.value); };
    children[image].clear();
    failedImages.erase(image);
    for (const auto& entry : entries)
    {
        if (entry.kind == DiscEntryKind::file || entry.capability.isNotEmpty()) continue;
        const auto name = key(entry.id);
        virtualFolders[name] = { image, entry.displayName, entry.id };
        children[key(entry.parent)].insert(name);
    }
    // Re-materialise only this image's children, without changing the user's selection.
    std::function<void(juce::TreeViewItem*)> update = [&](juce::TreeViewItem* item)
    {
        if (! item) return;
        if (item->getUniqueName() == image)
        {
            item->clearSubItems();
            for (const auto& child : children[image]) item->addSubItem(new Item(*this, child));
            item->setOpen(true);
            item->treeHasChanged();
            return;
        }
        for (int i = 0; i < item->getNumSubItems(); ++i) update(item->getSubItem(i));
    };
    update(tree.getRootItem());
}
void FolderTree::setDiscError(const juce::String& image, bool failed)
{
    if (failed) failedImages.insert(image); else failedImages.erase(image);
    tree.repaint();
}
}
