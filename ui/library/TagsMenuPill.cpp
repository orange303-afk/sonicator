#include "TagsMenuPill.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"

namespace sonicator
{
TagsMenuPill::TagsMenuPill(std::shared_ptr<juce::Point<int>> screenCentre,
                           std::function<void()> hovered)
    : juce::PopupMenu::CustomComponent(true),
      centre(std::move(screenCentre)), onHover(std::move(hovered))
{
}

void TagsMenuPill::getIdealSize(int& idealWidth, int& idealHeight)
{
    idealWidth = 230;
    idealHeight = 36;
}

void TagsMenuPill::resized()
{
    if (centre != nullptr) *centre = getScreenBounds().getCentre();
}

void TagsMenuPill::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(6.0f, 3.0f);
    const bool highlighted = isItemHighlighted();
    g.setColour(highlighted ? theme::raised.brighter(0.12f) : theme::raised);
    g.fillRoundedRectangle(r, 13.0f);
    g.setColour(highlighted ? theme::accent.withAlpha(0.55f) : theme::line);
    g.drawRoundedRectangle(r, 13.0f, 1.0f);

    const float dotX = r.getX() + 12.0f;
    g.setColour(theme::accent);
    g.fillEllipse(dotX - 3.5f, r.getCentreY() - 3.5f, 7.0f, 7.0f);

    g.setColour(theme::text);
    g.setFont(fonts::font(12.5f, fonts::Weight::medium));
    auto textArea = r.withTrimmedLeft(24.0f).withTrimmedRight(22.0f);
    g.drawText("Tags", textArea, juce::Justification::centredLeft, false);

    g.setColour(theme::muted);
    g.setFont(fonts::font(13.0f));
    g.drawText(juce::String::charToString((juce::juce_wchar) 0x203A),
               r.withTrimmedRight(10.0f), juce::Justification::centredRight, false);
}

void TagsMenuPill::mouseEnter(const juce::MouseEvent&)
{
    if (centre != nullptr) *centre = getScreenBounds().getCentre();
    startTimer(200);
}

void TagsMenuPill::mouseExit(const juce::MouseEvent&)
{
    stopTimer();
}

void TagsMenuPill::timerCallback()
{
    stopTimer();
    if (onHover) onHover();
}
} // namespace sonicator
