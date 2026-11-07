#include "ThemedDialogs.h"
namespace sonicator::dialogs {
namespace {
void centre(juce::AlertWindow& window, juce::Component& owner)
{
    window.centreAroundComponent(&owner, window.getWidth(), window.getHeight());
}
}
void showAlert(juce::Component& owner, const juce::String& title, const juce::String& message,
               std::initializer_list<DialogButton> buttons, std::function<void(int)> onResult,
               juce::MessageBoxIconType icon)
{
    auto* window = new juce::AlertWindow(title, message, icon);
    window->setLookAndFeel(&owner.getLookAndFeel());
    const int count = (int) buttons.size();
    int index = 0;
    for (const auto& button : buttons)
    {
        juce::KeyPress key;
        if (index == 0) key = juce::KeyPress(juce::KeyPress::returnKey);
        else if (index == count - 1) key = juce::KeyPress(juce::KeyPress::escapeKey);
        window->addButton(button.text, button.id, key);
        ++index;
    }
    centre(*window, owner);
    juce::Component::SafePointer<juce::AlertWindow> safeWindow(window);
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeWindow, callback = std::move(onResult)](int result)
        {
            if (callback) callback(result);
            juce::ignoreUnused(safeWindow);
        }), true);
}
void showPrompt(juce::Component& owner, const juce::String& title, const juce::String& label,
                const juce::String& initial, std::function<void(const juce::String&)> onAccept)
{
    auto* window = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon);
    window->setLookAndFeel(&owner.getLookAndFeel());
    window->addTextEditor("value", initial, label);
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    centre(*window, owner);
    juce::Component::SafePointer<juce::AlertWindow> safeWindow(window);
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeWindow, callback = std::move(onAccept)](int result)
        {
            if (safeWindow == nullptr || result == 0 || callback == nullptr) return;
            callback(safeWindow->getTextEditorContents("value"));
        }), true);
}
} // namespace sonicator::dialogs
