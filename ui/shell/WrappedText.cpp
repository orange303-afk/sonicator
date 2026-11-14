#include "WrappedText.h"
#include <cmath>

namespace sonicator::studio
{
void Paragraph::setText(const juce::String& value, juce::Font newFont, juce::Colour newColour,
                        int extraLineSpacing)
{
    text = value;
    font = newFont;
    colour = newColour;
    lineSpacing = extraLineSpacing;
    // The text is painted, not held by a Label, so the accessible name has to carry it.
    setTitle(text);
    repaint();
}

void Paragraph::setText(const juce::String& value)
{
    text = value;
    setTitle(text);
    repaint();
}

void Paragraph::setTextColour(juce::Colour newColour)
{
    colour = newColour;
    repaint();
}

juce::TextLayout Paragraph::build(int width) const
{
    juce::AttributedString attributed;
    attributed.setJustification(juce::Justification::topLeft);
    attributed.setLineSpacing((float) lineSpacing);
    attributed.append(text, font, colour);

    juce::TextLayout layout;
    layout.createLayout(attributed, (float) juce::jmax(1, width), 10000.0f);
    return layout;
}

int Paragraph::heightForWidth(int width) const
{
    if (text.isEmpty()) return 0;
    return (int) std::ceil(build(width).getHeight()) + 1;
}

void Paragraph::paint(juce::Graphics& g)
{
    if (text.isEmpty()) return;
    build(getWidth()).draw(g, getLocalBounds().toFloat());
}
} // namespace sonicator::studio
