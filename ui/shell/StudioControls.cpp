#include "StudioControls.h"
#include "Theme.h"

namespace sonicator::studio
{
IconButton::IconButton(ActionIcon icon, const juce::String& accessibleName)
    : juce::TextButton(accessibleName)
{
    setIcon(icon);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}
ActionIcon IconButton::icon() const noexcept
{
    return (ActionIcon) iconProperty(*this);
}
void IconButton::setIcon(ActionIcon icon)
{
    studio::setIcon(*this, icon);
}

Chip::Chip(const juce::String& text, bool small) : juce::TextButton(text)
{
    getProperties().set(properties::chip, small ? 2 : 1);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}
void Chip::setTrailingIcon(ActionIcon icon)
{
    getProperties().set(properties::trailingIcon, (int) icon);
    repaint();
}

CheckChip::CheckChip(const juce::String& text, bool small) : juce::ToggleButton(text)
{
    getProperties().set(properties::checkChip, small ? 2 : 1);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void StatTile::setStat(const Stat& next)
{
    data = next;
    // The card owns the tooltip (the whole path), so a tile never takes the mouse.
    setInterceptsMouseClicks(false, false);
    setTitle(data.value + ", " + data.caption);   // what assistive tools read for this paint-only text
    repaint();
}

int StatTile::preferredHeight()
{
    return juce::roundToInt(statValueFont().getHeight() + statCaptionFont().getHeight()) + 2;
}

void StatTile::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    const int valueHeight = juce::roundToInt(statValueFont().getHeight());
    g.setColour(theme::text);
    g.setFont(statValueFont());
    g.drawFittedText(data.value, area.removeFromTop(valueHeight), juce::Justification::centredLeft, 1);
    g.setColour(theme::muted);
    g.setFont(statCaptionFont());
    g.drawFittedText(data.caption, area, juce::Justification::topLeft, 2);
}

SectionCaption::SectionCaption(const juce::String& text)
{
    setText(text, juce::dontSendNotification);
    setFont(sectionFont());
    setColour(juce::Label::textColourId, theme::muted);
    setJustificationType(juce::Justification::centredLeft);
}
} // namespace sonicator::studio
