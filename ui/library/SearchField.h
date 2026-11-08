#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
class SearchField final : public juce::TextEditor
{
public:
    SearchField()
    {
        addAndMakeVisible(clearButton);
        clearButton.setComponentID("clearSearch");
        clearButton.setTooltip("Clear search");
        clearButton.setWantsKeyboardFocus(false);
        clearButton.onClick = [this] { clear(); if (onClear) onClear(); grabKeyboardFocus(); };
        setBorder(juce::BorderSize<int>(0, 0, 0, 30));
    }
    void resized() override
    {
        juce::TextEditor::resized();
        clearButton.setBounds(getWidth() - 29, 3, 26, juce::jmax(0, getHeight() - 6));
    }
    std::function<void()> onClear;
    std::function<void()> onHistoryRequest;
    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::TextEditor::mouseDown(event);
        if (event.mods.isLeftButtonDown() && onHistoryRequest) onHistoryRequest();
    }
private:
    juce::TextButton clearButton { juce::String::charToString(0x00d7) };
};
} // namespace sonicator
