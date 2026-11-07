#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
class SearchField final : public juce::TextEditor
{
public:
    std::function<void()> onHistoryRequest;
    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::TextEditor::mouseDown(event);
        if (event.mods.isLeftButtonDown() && onHistoryRequest) onHistoryRequest();
    }
};
} // namespace sonicator
