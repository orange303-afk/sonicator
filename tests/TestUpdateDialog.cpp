#include <catch2/catch_test_macros.hpp>
#include "ui/shell/Theme.h"
#include "ui/updates/UpdateDialog.h"

using namespace sonicator;
using namespace sonicator::ui::updates;

namespace
{
sonicator::updates::Release releaseWithNotes(juce::String notes)
{
    updates::Release release;
    release.tag = "v0.3.3";
    release.url = "https://example.test/tag";
    release.notes = std::move(notes);
    release.published = true;
    release.newer = true;
    return release;
}
} // namespace

TEST_CASE("The update dialog shows the notes of the new version", "[updates][ui]")
{
    const auto release = releaseWithNotes("## What changed\n\n"
                                          "- The Cloud map follows the tag and type filter.\n"
                                          "- The type model answers on its own.");
    UpdateDialogContent content(release);
    content.setSize(560, 560);

    auto* notes = dynamic_cast<juce::TextEditor*>(content.findChildWithID("updateDialogNotes"));
    REQUIRE(notes != nullptr);
    CHECK(notes->getText() == release.notes);
    CHECK(notes->isMultiLine());
    CHECK(notes->isReadOnly());

    auto* kofi = dynamic_cast<juce::HyperlinkButton*>(content.findChildWithID("updateDialogKofi"));
    auto* boosty = dynamic_cast<juce::HyperlinkButton*>(content.findChildWithID("updateDialogBoosty"));
    REQUIRE(kofi != nullptr);
    REQUIRE(boosty != nullptr);
    CHECK(kofi->getURL() == juce::URL("https://ko-fi.com/ilyaorange"));
    CHECK(boosty->getURL() == juce::URL("https://boosty.to/ilyaorange"));
    CHECK(kofi->findColour(juce::HyperlinkButton::textColourId) == theme::accent);
    CHECK(boosty->findColour(juce::HyperlinkButton::textColourId) == theme::accent);

    CHECK(content.findChildWithID("updateDialogDownload") != nullptr);
    CHECK(content.findChildWithID("updateDialogReleaseNotes") != nullptr);
    CHECK(content.findChildWithID("updateDialogGumroad") != nullptr);
    CHECK(content.findChildWithID("updateDialogLater") != nullptr);
}

TEST_CASE("Missing release notes still leave a downloadable update", "[updates][ui]")
{
    for (const auto* notes : { "", "   \n\t " })
    {
        UpdateDialogContent content(releaseWithNotes(notes));
        content.setSize(560, 560);
        auto* editor = dynamic_cast<juce::TextEditor*>(content.findChildWithID("updateDialogNotes"));
        REQUIRE(editor != nullptr);
        CHECK(editor->getText() == "Release notes are unavailable.");
        CHECK(content.findChildWithID("updateDialogDownload") != nullptr);
    }
}

TEST_CASE("Long notes keep the buttons and the support links on screen", "[updates][ui]")
{
    juce::String longNotes;
    for (int i = 0; i < 200; ++i)
        longNotes += "- A long line of release notes that would push anything below it out of a "
                     "fixed dialog when the field did not scroll.\n";
    UpdateDialogContent content(releaseWithNotes(longNotes));
    content.setSize(560, 560);

    auto* notes = content.findChildWithID("updateDialogNotes");
    auto* kofi = content.findChildWithID("updateDialogKofi");
    auto* download = content.findChildWithID("updateDialogDownload");
    auto* later = content.findChildWithID("updateDialogLater");
    REQUIRE(notes != nullptr);
    REQUIRE(kofi != nullptr);
    REQUIRE(download != nullptr);
    REQUIRE(later != nullptr);

    const auto bounds = content.getLocalBounds();
    for (auto* component : { notes, kofi, download, later })
    {
        CHECK(bounds.contains(component->getBounds()));
    }
    // The notes field takes the room between the header and the support row, and the actions stay
    // below both.
    CHECK(notes->getHeight() > 200);
    CHECK(kofi->getBounds().getBottom() < download->getBounds().getY());
    CHECK(later->getBounds().getRight() <= bounds.getRight());

    // Painting must not need a window: the dialog is drawn offscreen in tests and in the plug-in.
    juce::Image image(juce::Image::ARGB, 560, 560, true);
    juce::Graphics g(image);
    content.paint(g);
    CHECK(image.getWidth() == 560);
}
