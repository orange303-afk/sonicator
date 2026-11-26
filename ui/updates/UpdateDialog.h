#pragma once
#include "core/updates/UpdateCheck.h"
#include "ui/shell/DialogSurface.h"
#include "ui/shell/ThemedDialogs.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator::ui::updates
{
// The "a newer release is available" dialog's content: the notes of the new version in a
// read-only scrolling field, the support links under them and the download/notes/Gumroad/Later
// actions. Exposed so a test can check the notes fallback and the support links without opening a
// window.
class UpdateDialogContent final : public dialogs::EmbeddedDialogContent
{
public:
    explicit UpdateDialogContent(const sonicator::updates::Release& release);

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void addButton(juce::TextButton& button, const juce::String& text, const juce::String& id,
                   std::function<void()> action);
    void close();

    dialogs::Surface surface;
    juce::Label title, subtitle, notesCaption, supportCaption;
    juce::TextEditor notes;
    juce::HyperlinkButton kofi, boosty;
    juce::TextButton download, releaseNotes, gumroad, later;
    std::array<juce::TextButton*, 4> buttonOrder {};
    int buttonCount = 0;
    juce::String downloadUrl, pageUrl;
};

// Opens the dialog. Inside a DAW plug-in it is embedded in the editor; elsewhere it is its own
// themed window.
void showUpdateDialog(juce::Component& owner, const sonicator::updates::Release& release);
} // namespace sonicator::ui::updates
