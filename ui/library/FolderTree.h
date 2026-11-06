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
    void resized() override;
    std::function<void(juce::String)> onFolder;
private:
    class Item;
    juce::TreeView tree;
    std::unique_ptr<Item> root;
    std::map<juce::String,std::set<juce::String>> children;
    juce::StringArray roots;
    bool restoring = false;
};
}
