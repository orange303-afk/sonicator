#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
namespace sonicator::dialogs {
struct DialogButton
{
    juce::String text;
    int id = 0;
};
// Creates a modal AlertWindow that uses the owner's Sonicator LookAndFeel, so it follows the
// current theme instead of JUCE's default alert colours and buttons.
void showAlert(juce::Component& owner, const juce::String& title, const juce::String& message,
               std::initializer_list<DialogButton> buttons, std::function<void(int)> onResult,
               juce::MessageBoxIconType icon = juce::MessageBoxIconType::NoIcon);
// Text prompt with the same themed window. The callback runs only for OK.
void showPrompt(juce::Component& owner, const juce::String& title, const juce::String& label,
                const juce::String& initial, std::function<void(const juce::String&)> onAccept);
} // namespace sonicator::dialogs
