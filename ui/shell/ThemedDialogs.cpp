#include "ThemedDialogs.h"
#include "AppFonts.h"
#include "Theme.h"
#include <cmath>

namespace sonicator::dialogs
{
namespace
{
void centre(juce::AlertWindow& window, juce::Component& owner)
{
    window.centreAroundComponent(&owner, window.getWidth(), window.getHeight());
}

// One panel for both kinds of embedded dialog: a title, a body row and a footer of buttons.
class EmbeddedPanel final : public EmbeddedDialogContent
{
public:
    EmbeddedPanel(const juce::String& titleText, const juce::String& bodyText,
                  std::vector<DialogButton> dialogButtons)
        : buttons(std::move(dialogButtons))
    {
        title.setText(titleText, juce::dontSendNotification);
        title.setFont(fonts::font(15, fonts::Weight::bold));
        title.setColour(juce::Label::textColourId, theme::text);
        addAndMakeVisible(title);

        if (bodyText.isNotEmpty())
        {
            body.setText(bodyText, juce::dontSendNotification);
            body.setFont(fonts::font(12));
            body.setColour(juce::Label::textColourId, theme::muted);
            body.setJustificationType(juce::Justification::topLeft);
            body.setMinimumHorizontalScale(1.0f);
            bodyHeight = (int) std::ceil(bodyText.length() / 62.0 + 0.001) * 18;
            bodyHeight = juce::jmax(24, juce::jmin(bodyHeight, 6 * 18));
            addAndMakeVisible(body);
        }

        int index = 0;
        for (const auto& definition : buttons)
        {
            auto button = std::make_unique<juce::TextButton>(definition.text);
            button->setComponentID("embeddedDialogButton" + juce::String(index));
            const int id = definition.id;
            button->onClick = [this, id] { choose(id); };
            addAndMakeVisible(*button);
            buttonComponents.push_back(button.get());
            ownedButtons.push_back(std::move(button));
            ++index;
        }
    }

    void setField(const juce::String& labelText, const juce::String& initial)
    {
        fieldLabel.setText(labelText, juce::dontSendNotification);
        fieldLabel.setFont(fonts::font(12));
        fieldLabel.setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(fieldLabel);

        editor = std::make_unique<juce::TextEditor>();
        editor->setComponentID("embeddedDialogField");
        editor->setText(initial, juce::dontSendNotification);
        editor->setSelectAllWhenFocused(true);
        Surface::styleField(*editor);
        editor->onReturnKey = [this] { choose(1); };
        editor->onEscapeKey = [this] { choose(0); };
        addAndMakeVisible(*editor);
    }

    void paint(juce::Graphics& g) override { surface.paint(g, getLocalBounds()); }

    void resized() override
    {
        auto area = Surface::contentArea(getLocalBounds());
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(studio::spacingUnit);
        if (editor != nullptr)
        {
            fieldLabel.setBounds(area.removeFromTop(20));
            area.removeFromTop(studio::spacingTight);
            editor->setBounds(area.removeFromTop(studio::controlHeight));
        }
        else if (body.isVisible())
        {
            body.setBounds(area.removeFromTop(bodyHeight));
        }
        auto footer = Surface::footerArea(getLocalBounds());
        // The first declared button is the primary one and sits rightmost, as in a macOS dialog.
        for (auto* button : buttonComponents)
        {
            auto bounds = footer.removeFromRight(96);
            button->setBounds(bounds);
            footer.removeFromRight(studio::spacingTight);
        }
    }

    void parentHierarchyChanged() override
    {
        // The field takes focus, so Return and Escape work without a click.
        if (isShowing() && editor != nullptr) editor->grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (editor != nullptr) return false; // the field owns Return and Escape
        if (key == juce::KeyPress::escapeKey && ! buttons.empty())
        {
            choose(buttons.back().id);
            return true;
        }
        return false;
    }

    std::function<void(int, const juce::String&)> onResult;

private:
    void choose(int id)
    {
        // A host deletes this panel after the click returns, so the result runs first.
        const auto value = editor != nullptr ? editor->getText() : juce::String();
        if (onResult != nullptr) onResult(id, value);
        requestClose();
    }

    Surface surface;
    juce::Label title, body, fieldLabel;
    std::unique_ptr<juce::TextEditor> editor;
    std::vector<DialogButton> buttons;
    std::vector<juce::TextButton*> buttonComponents;
    std::vector<std::unique_ptr<juce::TextButton>> ownedButtons;
    int bodyHeight = 24;
};

int panelHeight(bool withField, const juce::String& message)
{
    const int lines = juce::jlimit(1, 6, (int) std::ceil(message.length() / 62.0 + 0.001));
    const int bodyHeight = withField ? 20 + studio::spacingTight + studio::controlHeight
                                     : juce::jmax(24, lines * 18);
    return 20 + 24 + studio::spacingUnit + bodyHeight + studio::spacingUnit
           + studio::iconButtonSize + 20;
}
} // namespace

EmbeddedDialogHost* embeddedHostFor(juce::Component& owner)
{
    for (auto* component = &owner; component != nullptr; component = component->getParentComponent())
        if (auto* host = dynamic_cast<EmbeddedDialogHost*>(component)) return host;
    return nullptr;
}

void showAlert(juce::Component& owner, const juce::String& title, const juce::String& message,
               std::initializer_list<DialogButton> buttons, std::function<void(int)> onResult,
               juce::MessageBoxIconType icon)
{
    if (auto* host = embeddedHostFor(owner))
    {
        auto panel = std::make_unique<EmbeddedPanel>(title, message,
                                                     std::vector<DialogButton>(buttons));
        // The host may still refuse; the copy keeps the window fallback usable.
        panel->onResult = [callback = onResult](int result, const juce::String&)
        {
            if (callback != nullptr) callback(result);
        };
        panel->setSize(420, panelHeight(false, message));
        if (host->showEmbeddedDialog(std::move(panel))) return;
    }

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
    if (auto* host = embeddedHostFor(owner))
    {
        auto panel = std::make_unique<EmbeddedPanel>(title, juce::String(),
                                                     std::vector<DialogButton> {
                                                         { "OK", 1 }, { "Cancel", 0 } });
        panel->setField(label, initial);
        panel->onResult = [callback = onAccept](int result, const juce::String& value)
        {
            if (result == 0 || callback == nullptr) return;
            callback(value);
        };
        panel->setSize(420, panelHeight(true, {}));
        if (host->showEmbeddedDialog(std::move(panel))) return;
    }

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
