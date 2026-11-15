#pragma once
#include "DialogSurface.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace sonicator::dialogs
{
struct DialogButton
{
    juce::String text;
    int id = 0;
};

// A dialog drawn inside the owner instead of in a top-level window. A DAW plug-in implements this:
// a host does not reliably keep a plug-in's modal window above its own window, and a modal window
// that dismisses on "input outside" closes on the first click inside the DAW.
class EmbeddedDialogHost
{
public:
    virtual ~EmbeddedDialogHost() = default;
    // Takes ownership of the dialog. False means the host cannot show it and the caller falls
    // back to an ordinary window.
    virtual bool showEmbeddedDialog(std::unique_ptr<juce::Component> content) = 0;
};

// The dialog the host takes over. It asks for removal through `onClose`, which the host sets.
class EmbeddedDialogContent : public juce::Component
{
public:
    std::function<void()> onClose;
    void requestClose()
    {
        if (onClose != nullptr) onClose();
    }
};

// Walks up from `owner` to the nearest component that renders dialogs inside itself.
EmbeddedDialogHost* embeddedHostFor(juce::Component& owner);

// Creates a modal AlertWindow that uses the owner's Sonicator LookAndFeel, so it follows the
// current theme instead of JUCE's default alert colours and buttons. Inside an EmbeddedDialogHost
// the same dialog is drawn in the editor and `onResult` fires on the button's id.
void showAlert(juce::Component& owner, const juce::String& title, const juce::String& message,
               std::initializer_list<DialogButton> buttons, std::function<void(int)> onResult,
               juce::MessageBoxIconType icon = juce::MessageBoxIconType::NoIcon);
// Text prompt with the same themed window. The callback runs only for OK.
void showPrompt(juce::Component& owner, const juce::String& title, const juce::String& label,
                const juce::String& initial, std::function<void(const juce::String&)> onAccept);
} // namespace sonicator::dialogs
