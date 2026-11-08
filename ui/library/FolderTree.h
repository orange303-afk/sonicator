#pragma once
#include "core/database/SampleRepository.h"
#include "core/disc/DiscFilesystem.h"
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
    void refresh(SampleRepository&, const juce::StringArray& rootPaths, bool showAllSamples,
                 bool showIgnored = false);
    void resized() override;
    void clearSelection();
    // Opens the ancestors and selects a folder path. Returns false when it is not in the tree.
    bool selectPath(const juce::String& path);
    void lookAndFeelChanged() override;
    void setDiscFolders(const juce::String& image, DiscEntryId rootId, const std::vector<DiscEntry>&);
    void setDiscError(const juce::String& image, bool failed);
    std::function<void(const juce::String&, DiscEntryId)> onDiscFolder;
    std::function<void(juce::String)> onFolder;
    // Right-click on a folder. An empty path with allSamples means the virtual All samples node.
    std::function<void(const juce::String& path, bool allSamples, juce::Point<int> screenPosition)> onContextMenu;
private:
    class Item;
    juce::TreeView tree;
    std::unique_ptr<Item> root;
    std::map<juce::String,std::set<juce::String>> children;
    std::set<juce::String> images;
    std::set<juce::String> failedImages;
    struct VirtualFolder { juce::String image, name; DiscEntryId id; };
    std::map<juce::String, VirtualFolder> virtualFolders;
    juce::StringArray roots;
    bool restoring = false;
    bool showIgnored = false;
    std::vector<IgnoredPath> ignoredEntries;
    bool selectPath(juce::TreeViewItem& parent, const juce::String& path);
};
}
