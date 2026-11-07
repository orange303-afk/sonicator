#pragma once
#include "core/database/SampleRepository.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>
namespace sonicator {
// Only registered roots and catalog folders. Tree items materialise children on expansion.
class FolderTree final : public juce::Component
{
public:
    FolderTree();
    ~FolderTree() override;
    void refresh(SampleRepository&);
    // Builds a tree for an explicit root list. With showAllSamples=false the container root is
    // hidden and the supplied folders appear as top-level expandable entries.
    void refresh(SampleRepository&, const juce::StringArray& rootPaths, bool showAllSamples);
    void resized() override;
    void clearSelection();
    // Opens the ancestors and selects a folder path. Returns false when it is not in the tree.
    bool selectPath(const juce::String& path);
    void lookAndFeelChanged() override;
    std::function<void(juce::String)> onFolder;
    // Right-click on a folder. An empty path with allSamples means the virtual All samples node.
    std::function<void(const juce::String& path, bool allSamples, juce::Point<int> screenPosition)> onContextMenu;
private:
    class Item;
    juce::TreeView tree;
    std::unique_ptr<Item> root;
    std::map<juce::String,std::set<juce::String>> children;
    juce::StringArray roots;
    bool restoring = false;
    bool selectPath(juce::TreeViewItem& parent, const juce::String& path);
};
}
