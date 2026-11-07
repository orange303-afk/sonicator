#include "FolderTree.h"
#include "ui/shell/Theme.h"
namespace sonicator {
class FolderTree::Item final : public juce::TreeViewItem
{
public:
    Item(FolderTree& o,juce::String p,bool top = false) : owner(o),path(std::move(p)),all(top) {}
    bool mightContainSubItems() override { return all || ! owner.children[path].empty(); }
    juce::String getUniqueName() const override { return all ? "All samples" : path; }
    int getItemHeight() const override { return 26; }
    void paintItem(juce::Graphics& g,int w,int h) override
    {
        if (isSelected()) { g.setColour(theme::selection); g.fillAll(); }
        g.setColour(theme::text); g.setFont(13.0f);
        g.drawText(all ? "All samples" : juce::File(path).getFileName(), 3,0,w-6,h,juce::Justification::centredLeft,true);
    }
    void itemSelectionChanged(bool selected) override { if (selected && ! owner.restoring && owner.onFolder) owner.onFolder(all ? juce::String() : path); }
    void itemClicked(const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || owner.onContextMenu == nullptr) return;
        if (! isSelected()) setSelected(true, true);
        owner.onContextMenu(all ? juce::String() : path, all, e.getScreenPosition());
    }
    void itemOpennessChanged(bool open) override
    {
        if (! open || getNumSubItems() != 0) return;
        if (all) { for (const auto& p : owner.roots) addSubItem(new Item(owner,p)); }
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
void FolderTree::lookAndFeelChanged()
{
    tree.setColour(juce::TreeView::backgroundColourId,juce::Colours::transparentBlack);
    tree.repaint();
}
void FolderTree::refresh(SampleRepository& repo)
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoring,true);
    auto openness = tree.getOpennessState(false);
    tree.setRootItem(nullptr); root.reset(); children.clear(); roots = repo.roots();
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
    root = std::make_unique<Item>(*this,juce::String(),true); tree.setRootItem(root.get()); root->setOpen(true);
    if (openness) tree.restoreOpennessState(*openness,true);
}
}
