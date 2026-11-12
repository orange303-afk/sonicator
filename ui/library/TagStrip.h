#pragma once
#include "core/database/LibraryQuery.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
namespace sonicator {
// Read-only strip of coloured tag chips, used by the sample inspector.
class TagStrip final : public juce::Component
{
public:
    void setTags(std::vector<Tag>);
    const std::vector<Tag>& getTags() const { return tags; }
    // Height needed for up to maxRows chip rows at the given width.
    int getPreferredHeight(int width, int maxRows = 3) const;
    void paint(juce::Graphics&) override;
    void resized() override;
    // Alt+click removes a tag; the modifier is passed through so the caller decides.
    std::function<void(const Tag&, const juce::ModifierKeys&)> onTagClicked;
private:
    std::vector<Tag> tags;
    std::vector<juce::Rectangle<int>> chipBounds;
    void layout(int width);
    void mouseDown(const juce::MouseEvent&) override;
};
} // namespace sonicator
