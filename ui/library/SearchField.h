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
            if (onClear) onClear();
            grabKeyboardFocus();
        };
        // Room for the magnifier on the left and the clear control on the right.
        setBorder(juce::BorderSize<int>(0, 26, 0, 30));
        // The stock editor font is 14 px and skips the calibrated scale; the field has to read at
        // the same height as the rest of the header.
        setFont(studio::controlFont());
        onTextChange = [this]
        {
            if (onTextChanged) onTextChanged();
        };
    }
    ActionIcon leadingIcon() const { return leading; }
    // The owner's single text callback: the field forwards it and nothing else.
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
    }
    std::function<void()> onClear;
    std::function<void()> onHistoryRequest;
    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::TextEditor::mouseDown(event);
        if (event.mods.isLeftButtonDown() && onHistoryRequest) onHistoryRequest();
    }
private:
    ActionIcon leading = ActionIcon::search;
    // The control stays in place whether or not there is text: it is how the field reads as
    // clearable at a glance, and a control that vanishes is one a user hunts for.
    juce::TextButton clearButton { juce::String::charToString(0x00d7) };
};
} // namespace sonicator
