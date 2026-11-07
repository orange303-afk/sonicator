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
        const bool missing = path.isNotEmpty() && ! juce::File(path).isDirectory();
        g.setColour(missing ? theme::muted : theme::text); g.setFont(13.0f);
        g.drawText(all ? "All samples" : juce::File(path).getFileName(), 3,0,w-6,h,juce::Justification::centredLeft,true);
    }
    void itemSelectionChanged(bool selected) override
    {
        if (selected && ! owner.restoring && owner.onFolder && (all || path.isNotEmpty()))
            owner.onFolder(all ? juce::String() : path);
    }
    void itemClicked(const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || owner.onContextMenu == nullptr) return;
        if (! all && path.isEmpty()) return;
        if (! isSelected()) setSelected(true, true);
        owner.onContextMenu(all ? juce::String() : path, all, e.getScreenPosition());
    }
    void itemOpennessChanged(bool open) override
    {
        if (! open || getNumSubItems() != 0) return;
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
    refresh(repo, repo.roots(), true);
}
void FolderTree::refresh(SampleRepository& repo, const juce::StringArray& rootPaths, bool showAllSamples)
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoring,true);
    auto openness = tree.getOpennessState(false);
    tree.setRootItem(nullptr); root.reset(); children.clear(); roots = rootPaths;
    tree.setRootItemVisible(showAllSamples);
    std::set<juce::String> visitedFolders;
    int64_t last = 0;
    for (;;)
    {
        auto rows = repo.pathsAfterId(last,5000);
        if (rows.empty()) break;
        last = rows.back().id;
        for (const auto& row : rows)
        {
            auto folder = juce::File(row.path).getParentDirectory();
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
    }
    root = std::make_unique<Item>(*this,juce::String(),showAllSamples);
    tree.setRootItem(root.get()); root->setOpen(true);
    if (openness) tree.restoreOpennessState(*openness,true);
}
}
