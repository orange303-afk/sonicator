#include "TagStrip.h"
#include "ui/shell/Theme.h"
namespace sonicator {
namespace {
constexpr int chipHeight = 22, gap = 5, padding = 9, dotSize = 6;
juce::Font chipFont() { return juce::FontOptions(11); }
int chipWidth(const Tag& tag)
{
    return padding + dotSize + 6 + juce::GlyphArrangement::getStringWidthInt(chipFont(), tag.name) + padding;
}
}
void TagStrip::setTags(std::vector<Tag> values)
{
    tags = std::move(values);
    layout(getWidth());
    repaint();
}
int TagStrip::getPreferredHeight(int width, int maxRows) const
{
    if (tags.empty()) return 24;
    int rows = 1, x = 0;
    for (const auto& tag : tags)
    {
        const int w = chipWidth(tag);
        if (x > 0 && x + w > width) { ++rows; x = 0; }
        x += w + gap;
    }
    return juce::jmin(rows, maxRows) * chipHeight + juce::jmax(0, juce::jmin(rows, maxRows) - 1) * gap;
}
void TagStrip::resized()
{
    layout(getWidth());
}
void TagStrip::layout(int width)
{
    chipBounds.clear();
    if (width <= 0) return;
    int x = 0, y = 0;
    for (const auto& tag : tags)
    {
        const int w = chipWidth(tag);
        if (x > 0 && x + w > width) { x = 0; y += chipHeight + gap; }
        chipBounds.emplace_back(x, y, w, chipHeight);
        x += w + gap;
    }
}
void TagStrip::paint(juce::Graphics& g)
{
    if (tags.empty())
    {
        g.setColour(theme::muted); g.setFont(juce::FontOptions(11));
        g.drawText("No tags", 0, 0, getWidth(), 22, juce::Justification::centredLeft, true);
        return;
    }
    g.setFont(chipFont());
    for (size_t i = 0; i < tags.size(); ++i)
    {
        if (i >= chipBounds.size()) break;
        const auto& tag = tags[i];
        const auto colour = juce::Colour(tag.colour);
        auto bounds = chipBounds[i].toFloat();
        if (bounds.getY() >= (float) getHeight()) break;
        g.setColour(colour.withAlpha(0.16f)); g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(colour.withAlpha(0.55f)); g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(colour);
        g.fillEllipse(bounds.getX() + padding, bounds.getCentreY() - dotSize / 2.0f, dotSize, dotSize);
        g.setColour(theme::tagText(colour));
        g.drawText(tag.name, chipBounds[i].withTrimmedLeft(padding + dotSize + 6),
                   juce::Justification::centredLeft, true);
    }
}
} // namespace sonicator
