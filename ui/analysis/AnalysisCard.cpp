#include "AnalysisCard.h"
#include "ui/shell/Theme.h"
#include <cmath>

namespace sonicator
{
namespace
{
void styleLabel(juce::Label& label, juce::Font font, juce::Colour colour)
{
    label.setFont(font);
    label.setColour(juce::Label::textColourId, colour);
    label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label.setJustificationType(juce::Justification::topLeft);
    label.setMinimumHorizontalScale(1.0f);
    label.setInterceptsMouseClicks(false, false);
}
} // namespace

class WrappedNote final : public juce::Component
{
public:
    void setText(const juce::String& text)
    {
        body = text;
        repaint();
    }
    const juce::String& getText() const noexcept { return body; }
    void paint(juce::Graphics& g) override
    {
        if (body.isEmpty()) return;
        juce::AttributedString attributed;
        attributed.setJustification(juce::Justification::topLeft);
        attributed.setColour(theme::muted);
        attributed.append(body, studio::captionFont());
        juce::TextLayout layout;
        layout.createLayout(attributed, (float) getWidth());
        layout.draw(g, getLocalBounds().toFloat());
    }
private:
    juce::String body;
};

AnalysisCard::~AnalysisCard() = default;

AnalysisCard::AnalysisCard()
{
    addChildComponent(title);
    addChildComponent(meta);
    styleLabel(title, juce::Font(juce::FontOptions(studio::bodyFont().getHeight(), juce::Font::bold)),
               theme::text);
    styleLabel(meta, studio::captionFont(), theme::muted);
}

void AnalysisCard::setContent(const AnalysisContent& next)
{
    data = next;
    title.setText(data.title, juce::dontSendNotification);
    meta.setText(data.meta, juce::dontSendNotification);
    // Empty pieces are not children at all, so the card holds exactly what it shows.
    if (data.title.isNotEmpty()) addAndMakeVisible(title); else removeChildComponent(&title);
    if (data.meta.isNotEmpty()) addAndMakeVisible(meta); else removeChildComponent(&meta);

    tiles.clear();
    for (const auto& stat : data.stats)
    {
        auto tile = std::make_unique<studio::StatTile>();
        tile->setStat(stat);
        addAndMakeVisible(*tile);
        tiles.push_back(std::move(tile));
    }
    notes.clear();
    for (const auto& text : data.notes)
    {
        auto note = std::make_unique<WrappedNote>();
        note->setText(text);
        addAndMakeVisible(*note);
        notes.push_back(std::move(note));
    }
    setTooltip(data.tooltip);
    resized();
}

void AnalysisCard::showPlaceholder(const juce::String& text)
{
    AnalysisContent placeholder;
    placeholder.notes = { text };
    setContent(placeholder);
}

int AnalysisCard::columnsFor(int width)
{
    // The default analysis panel gives the card about 224 px, which is two columns; the 200 px
    // minimum panel gives it 154 px, which is one.
    return width >= 210 ? 2 : 1;
}

int AnalysisCard::wrappedHeight(const juce::String& text, const juce::Font& font, int width)
{
    if (text.isEmpty() || width <= 0) return 0;
    juce::AttributedString attributed;
    attributed.setJustification(juce::Justification::topLeft);
    attributed.append(text, font);
    juce::TextLayout layout;
    layout.createLayout(attributed, (float) width);
    // A label draws as many lines as fit: height / font height. Returning exactly lines * height
    // can truncate to one line through float rounding, so each line gets a pixel of slack.
    const float lineHeight = font.getHeight();
    const int lines = juce::jmax(1, (int) std::ceil(layout.getHeight() / lineHeight));
    return juce::roundToInt(lines * lineHeight) + 1;
}

int AnalysisCard::preferredHeight(int width) const
{
    if (width <= 0) return 0;
    int height = 0;
    if (data.title.isNotEmpty()) height += 20 + (data.meta.isNotEmpty() ? 2 : 0);
    if (data.meta.isNotEmpty()) height += 16;
    if (! data.stats.empty())
    {
        const int columns = columnsFor(width);
        const int rows = ((int) data.stats.size() + columns - 1) / columns;
        height += studio::spacingTight;
        height += rows * (studio::StatTile::preferredHeight() + studio::spacingTight);
    }
    for (const auto& note : data.notes)
        height += wrappedHeight(note, studio::captionFont(), width) + 6;
    return juce::jmax(20, height);
}

void AnalysisCard::resized()
{
    auto area = getLocalBounds();
    if (data.title.isNotEmpty())
    {
        title.setBounds(area.removeFromTop(20));
        if (data.meta.isNotEmpty()) area.removeFromTop(2);
    }
    if (data.meta.isNotEmpty()) meta.setBounds(area.removeFromTop(16));

    if (! tiles.empty())
    {
        area.removeFromTop(studio::spacingTight);
        const int columns = columnsFor(getWidth());
        const int gap = studio::spacing;
        const int tileHeight = studio::StatTile::preferredHeight();
        const int tileWidth = (area.getWidth() - gap * (columns - 1)) / juce::jmax(1, columns);
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            const int row = (int) i / columns;
            const int column = (int) i % columns;
            tiles[i]->setBounds(area.getX() + column * (tileWidth + gap),
                                area.getY() + row * (tileHeight + studio::spacingTight),
                                tileWidth, tileHeight);
        }
        const int rows = ((int) tiles.size() + columns - 1) / columns;
        area.removeFromTop(rows * (tileHeight + studio::spacingTight));
    }

    for (auto& note : notes)
    {
        const int height = wrappedHeight(note->getText(), studio::captionFont(), area.getWidth());
        note->setBounds(area.removeFromTop(height));
        area.removeFromTop(6);
    }
}

void AnalysisCard::paint(juce::Graphics&) {}
} // namespace sonicator
