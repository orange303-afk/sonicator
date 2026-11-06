#include "ExternalFileDrag.h"
#include <algorithm>
namespace sonicator {
juce::StringArray ExternalFileDrag::existingPaths(const juce::StringArray& candidates)
{
    juce::StringArray files;
    for (const auto& path : candidates)
        if (juce::File(path).existsAsFile()) files.add(path);
    return files;
}
juce::StringArray ExternalFileDrag::selectedPaths(const std::vector<SampleRecord>& visible,
                                                  const std::vector<int>& selectedRows)
{
    auto rows = selectedRows;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    juce::StringArray files;
    for (const int row : rows)
        if (row >= 0 && row < (int) visible.size())
            if (juce::File(visible[(size_t) row].filePath).existsAsFile())
                files.add(visible[(size_t) row].filePath);
    return files;
}
bool ExternalFileDrag::exceedsThreshold(int dx, int dy) { return dx * dx + dy * dy > 25; }
bool ExternalFileDrag::start(const juce::StringArray& candidates, juce::Component* source,
                             DragFunction drag)
{
    const auto files = existingPaths(candidates);
    if (files.isEmpty()) return false;
    if (drag) return drag(files, false, source);
    return juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, source);
}
} // namespace sonicator
