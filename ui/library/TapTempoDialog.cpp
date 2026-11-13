#include "TapTempoDialog.h"
#include "ui/shell/Theme.h"

namespace sonicator
{
TapTempoDialog::TapTempoDialog(juce::String titleText)
{
    title.setText(titleText, juce::dontSendNotification);
    title.setFont(juce::FontOptions(14, juce::Font::bold));
    title.setColour(juce::Label::textColourId, theme::text);
    title.setJustificationType(juce::Justification::centred);
    value.setText("\u2014", juce::dontSendNotification);
    value.setFont(juce::FontOptions(40, juce::Font::bold));
    value.setColour(juce::Label::textColourId, theme::accent);
    value.setJustificationType(juce::Justification::centred);
    for (auto* component : std::array<juce::Component*, 6>{ &title, &value, &tapButton, &loopButton,
                                                           &applyButton, &cancelButton })
        addAndMakeVisible(component);
    for (auto* button : std::array<juce::Button*, 4>{ &tapButton, &loopButton, &applyButton, &cancelButton })
    {
        // Space must reach keyPressed instead of clicking whichever button has focus.
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
    }
    tapButton.onClick = [this] { tap(); };
    loopButton.onClick = [this]
    {
        if (! loopAvailable) return;
        loopRunning = ! loopRunning;
        loopButton.setButtonText(loopRunning ? "Stop" : "Start loop");
        if (loopRunning)
        {
            if (onStartLoop) onStartLoop();
        }
        else if (onStopLoop) onStopLoop();
    };
    applyButton.onClick = [this] { apply(); };
    cancelButton.onClick = [this] { cancel(); };
    setWantsKeyboardFocus(true);
    setSize(340, 260);
    updateLabels();
}

TapTempoDialog::~TapTempoDialog()
{
    if (keyTarget != nullptr) keyTarget->removeKeyListener(this);
    // Any way of closing the window (Esc, Apply, the title-bar X) must not leave the preview
    // looping with no window to stop it.
    if (onStopLoop) onStopLoop();
}

void TapTempoDialog::setLoopAvailable(const bool available)
{
    loopAvailable = available;
    loopButton.setEnabled(available);
    if (! available) loopButton.setButtonText("Start loop");
}

void TapTempoDialog::tap()
{
    const auto now = clock ? clock() : (int64_t) juce::Time::getMillisecondCounterHiRes();
    tempo.tap(now);
    updateLabels();
}

void TapTempoDialog::updateLabels()
{
    value.setText(tempo.hasValue() ? juce::String(tempo.bpm(), 1) : juce::String("\u2014"),
                  juce::dontSendNotification);
    applyButton.setEnabled(tempo.hasValue());
}

bool TapTempoDialog::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        tap();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        apply();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        cancel();
        return true;
    }
    return false;
}

void TapTempoDialog::apply()
{
    if (! tempo.hasValue()) return;
    const auto bpm = tempo.bpm();
    if (loopRunning && onStopLoop) onStopLoop();
    loopRunning = false;
    if (onApply) onApply(bpm);
    closeDialog();
}

void TapTempoDialog::cancel()
{
    if (loopRunning && onStopLoop) onStopLoop();
    loopRunning = false;
    if (onCancel) onCancel();
    closeDialog();
}

void TapTempoDialog::closeDialog()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}

void TapTempoDialog::resized()
{
    auto area = getLocalBounds().reduced(16);
    title.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    value.setBounds(area.removeFromTop(64));
    area.removeFromTop(6);
    auto tapArea = area.removeFromTop(48);
    tapButton.setBounds(tapArea.reduced(40, 0));
    area.removeFromTop(8);
    loopButton.setBounds(area.removeFromTop(26));
    area.removeFromTop(8);
    auto buttons = area.removeFromTop(28);
    applyButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 4));
    cancelButton.setBounds(buttons.removeFromRight(buttons.getWidth() - 8));
}

void TapTempoDialog::parentHierarchyChanged()
{
    // The modal window itself takes focus, and its default keyPressed does not reach the content.
    // Listening on the top-level component makes Space/Return/Esc work regardless of who holds it.
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
    {
        if (keyTarget != top)
        {
            if (keyTarget != nullptr) keyTarget->removeKeyListener(this);
            keyTarget = top;
            top->addKeyListener(this);
        }
    }
    grabKeyboardFocus();
}

bool TapTempoDialog::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return keyPressed(key);
}

void TapTempoDialog::visibilityChanged()
{
    if (isVisible() && isShowing()) grabKeyboardFocus();
}

void TapTempoDialog::launch(juce::Component* owner, juce::String titleText,
                            std::function<void()> startLoop, std::function<void()> stopLoop,
                            std::function<void(double)> apply)
{
    auto dialog = std::make_unique<TapTempoDialog>(titleText);
    dialog->onStartLoop = std::move(startLoop);
    dialog->onStopLoop = std::move(stopLoop);
    dialog->onApply = std::move(apply);
    dialog->setLoopAvailable(static_cast<bool>(dialog->onStartLoop) || static_cast<bool>(dialog->onStopLoop));
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog.release());
    options.dialogTitle = "Tap BPM";
    options.dialogBackgroundColour = theme::panel;
    options.escapeKeyTriggersCloseButton = false;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.componentToCentreAround = owner;
    auto* window = options.launchAsync();
    if (window != nullptr)
    {
        juce::Component::SafePointer<TapTempoDialog> safe(dialog.get());
        juce::MessageManager::callAsync([safe]
        {
            if (safe != nullptr) safe->grabKeyboardFocus();
        });
    }
}
} // namespace sonicator
