#include "UpdateDialog.h"
#include "core/Version.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/StudioControls.h"
#include "ui/shell/Theme.h"

namespace sonicator::ui::updates
{
namespace
{
constexpr const char* kofiUrl = "https://ko-fi.com/ilyaorange";
constexpr const char* boostyUrl = "https://boosty.to/ilyaorange";
constexpr const char* missingNotes = "Release notes are unavailable.";

juce::Label& styleLabel(juce::Label& label, float height, fonts::Weight weight, juce::Colour colour)
{
    label.setFont(fonts::font(height, weight));
    label.setColour(juce::Label::textColourId, colour);
    return label;
}

juce::Point<int> dialogSizeFor(juce::Component& owner, bool embedded)
{
    juce::Point<int> size { 560, 560 };
    if (! embedded) return size;
    if (owner.getWidth() > 0) size.x = juce::jmax(360, juce::jmin(size.x, owner.getWidth() - 40));
    if (owner.getHeight() > 0) size.y = juce::jmax(320, juce::jmin(size.y, owner.getHeight() - 40));
    return size;
}
} // namespace

UpdateDialogContent::UpdateDialogContent(const sonicator::updates::Release& release)
    : downloadUrl(sonicator::updates::downloadUrlFor(release, sonicator::updates::currentPlatform())),
      pageUrl(release.url)
{
    setComponentID("updateDialog");

    styleLabel(title, 16.0f, fonts::Weight::bold, theme::text);
    title.setText("SONICATOR " + release.tag + " is available", juce::dontSendNotification);
    addAndMakeVisible(title);

    styleLabel(subtitle, 12.5f, fonts::Weight::regular, theme::muted);
    subtitle.setText("You are running " + juce::String(version()) + ". What changed in the new "
                     "version is below; download the installer for this platform from GitHub, "
                     "read the release page, or get it from Gumroad.",
                     juce::dontSendNotification);
    subtitle.setJustificationType(juce::Justification::topLeft);
    subtitle.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(subtitle);

    styleLabel(notesCaption, 11.0f, fonts::Weight::semiBold, theme::muted);
    notesCaption.setText("WHAT CHANGED IN " + release.tag, juce::dontSendNotification);
    addAndMakeVisible(notesCaption);

    notes.setComponentID("updateDialogNotes");
    notes.setMultiLine(true);
    notes.setReadOnly(true);
    notes.setScrollbarsShown(true);
    notes.setCaretVisible(false);
    notes.setPopupMenuEnabled(false);
    notes.setFont(fonts::font(12.5f));
    const auto trimmedNotes = release.notes.trim();
    notes.setText(trimmedNotes.isNotEmpty() ? trimmedNotes : juce::String(missingNotes),
                  juce::dontSendNotification);
    dialogs::Surface::styleField(notes);
    addAndMakeVisible(notes);

    // Support sits under the notes in the accent colour, so it reads as a separate offer and never
    // as one of the download actions.
    styleLabel(supportCaption, 12.0f, fonts::Weight::semiBold, theme::accent);
    supportCaption.setText("Support SONICATOR", juce::dontSendNotification);
    addAndMakeVisible(supportCaption);

    kofi.setComponentID("updateDialogKofi");
    kofi.setButtonText("Ko-fi");
    kofi.setURL(juce::URL(kofiUrl));
    kofi.setTooltip("Support development on Ko-fi");
    kofi.setColour(juce::HyperlinkButton::textColourId, theme::accent);
    kofi.setFont(fonts::font(13.0f, fonts::Weight::semiBold), false);
    addAndMakeVisible(kofi);

    boosty.setComponentID("updateDialogBoosty");
    boosty.setButtonText("Boosty");
    boosty.setURL(juce::URL(boostyUrl));
    boosty.setTooltip("Support development on Boosty");
    boosty.setColour(juce::HyperlinkButton::textColourId, theme::accent);
    boosty.setFont(fonts::font(13.0f, fonts::Weight::semiBold), false);
    addAndMakeVisible(boosty);

    addButton(download, "Download from GitHub", "updateDialogDownload", [this]
    {
        juce::URL(downloadUrl).launchInDefaultBrowser();
    });
    addButton(releaseNotes, "Release notes", "updateDialogReleaseNotes", [this]
    {
        juce::URL(pageUrl).launchInDefaultBrowser();
    });
    addButton(gumroad, "Gumroad", "updateDialogGumroad", [this]
    {
        juce::URL(sonicator::updates::gumroadUrl()).launchInDefaultBrowser();
    });
    addButton(later, "Later", "updateDialogLater", [] {});
}

void UpdateDialogContent::addButton(juce::TextButton& button, const juce::String& text,
                                    const juce::String& id, std::function<void()> action)
{
    button.setComponentID(id);
    button.setButtonText(text);
    button.onClick = [this, action = std::move(action)]
    {
        action();
        close();
    };
    addAndMakeVisible(button);
    if (buttonCount < (int) buttonOrder.size()) buttonOrder[(size_t) buttonCount++] = &button;
}

void UpdateDialogContent::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
    // Inside a DAW editor the dialog is a plain child of the plug-in window: the host removes it.
    else requestClose();
}

void UpdateDialogContent::paint(juce::Graphics& g)
{
    surface.paint(g, getLocalBounds());
}

void UpdateDialogContent::resized()
{
    const auto bounds = getLocalBounds();
    auto area = dialogs::Surface::contentArea(bounds);
    title.setBounds(area.removeFromTop(24));
    area.removeFromTop(studio::spacingUnit);
    subtitle.setBounds(area.removeFromTop(juce::jmin(48, juce::jmax(34, area.getHeight() / 5))));
    area.removeFromTop(studio::spacingWide);

    auto footer = dialogs::Surface::footerArea(bounds);
    auto support = footer.withY(footer.getY() - 26 - studio::spacingTight).withHeight(26);
    area.setBottom(support.getY() - studio::spacingTight);

    notesCaption.setBounds(area.removeFromTop(16));
    area.removeFromTop(studio::spacingTight);
    notes.setBounds(area);

    supportCaption.setBounds(support.removeFromLeft(
        juce::GlyphArrangement::getStringWidthInt(fonts::font(12.0f, fonts::Weight::semiBold),
                                                  supportCaption.getText()) + 16));
    kofi.setBounds(support.removeFromLeft(58));
    boosty.setBounds(support.removeFromLeft(70));

    // The first declared button is the primary one and sits rightmost, as in a macOS dialog.
    for (int i = 0; i < buttonCount; ++i)
    {
        buttonOrder[(size_t) i]->setBounds(footer.removeFromRight(112));
        footer.removeFromRight(studio::spacingTight);
    }
}

bool UpdateDialogContent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void showUpdateDialog(juce::Component& owner, const sonicator::updates::Release& release)
{
    auto content = std::make_unique<UpdateDialogContent>(release);
    if (auto* host = dialogs::embeddedHostFor(owner))
    {
        const auto size = dialogSizeFor(owner, true);
        content->setSize(size.x, size.y);
        if (host->showEmbeddedDialog(std::move(content))) return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = "Update";
    options.dialogBackgroundColour = theme::panel;
    options.escapeKeyTriggersCloseButton = false;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.componentToCentreAround = &owner;
    const auto size = dialogSizeFor(owner, false);
    if (auto* window = options.launchAsync())
    {
        window->setSize(size.x, size.y);
        window->setLookAndFeel(&owner.getLookAndFeel());
    }
}
} // namespace sonicator::ui::updates
