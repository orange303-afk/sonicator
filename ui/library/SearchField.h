#pragma once
#include "ui/shell/ActionIcons.h"
#include "ui/shell/DesignTokens.h"
#include "ui/shell/Theme.h"
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
        clearButton.onClick = [this]
        {
            clear();
            updateClearControl();   // clear() does not send a change message
            if (onClear) onClear();
            grabKeyboardFocus();
        };
        // Room for the magnifier on the left and the clear control on the right.
        setBorder(juce::BorderSize<int>(0, 26, 0, 30));
        clearButton.setVisible(false);
        onTextChange = [this]
        {
            updateClearControl();
            if (onTextChanged) onTextChanged();
        };
    }
    ActionIcon leadingIcon() const { return leading; }
    // TextEditor::clear() sends no change message, so the clear control is updated here.
    void clear()
    {
        juce::TextEditor::clear();
        updateClearControl();
    }
    // The owner's single text callback: the field keeps its own clear control in step first.
    std::function<void()> onTextChanged;
    void paintOverChildren(juce::Graphics& g) override
    {
        // The placeholder hint and the field outline live in the base implementation.
        juce::TextEditor::paintOverChildren(g);
        g.setColour(theme::muted);
        drawActionIcon(g, leading,
                       juce::Rectangle<float>(studio::spacingTight,
                                              (float) getHeight() * 0.5f - 7.0f, 14.0f, 14.0f),
                       1.4f);
    }
    void resized() override
    {
        juce::TextEditor::resized();
        clearButton.setBounds(getWidth() - 29, 3, 26, juce::jmax(0, getHeight() - 6));
        updateClearControl();
    }
    std::function<void()> onClear;
    std::function<void()> onHistoryRequest;
    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::TextEditor::mouseDown(event);
        if (event.mods.isLeftButtonDown() && onHistoryRequest) onHistoryRequest();
    }
private:
    // The clear control only exists while there is something to clear.
    void updateClearControl() { clearButton.setVisible(getText().isNotEmpty()); }
    ActionIcon leading = ActionIcon::search;
    juce::TextButton clearButton { juce::String::charToString(0x00d7) };
};
} // namespace sonicator
