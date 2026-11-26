#include "SpectrogramRenderer.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"
#include <array>

namespace sonicator::spectrogramview
{
namespace
{
constexpr int lutSize = 128;

std::array<juce::PixelARGB, lutSize> buildLut(double floorDb)
{
    std::array<juce::PixelARGB, lutSize> lut {};
    for (int i = 0; i < lutSize; ++i)
    {
        const auto db = (float) floorDb + (0.0f - (float) floorDb) * (float) i / (float) (lutSize - 1);
        lut[(size_t) i] = colourForDb(db, floorDb).getPixelARGB();
    }
    return lut;
}
} // namespace

juce::Colour colourForDb(float db, double floorDb)
{
    const auto span = (float) (0.0 - floorDb);
    const auto level = span > 0.0f ? (db - (float) floorDb) / span : 0.0f;
    return theme::spectrogramColour(level);
}

juce::Image render(const SpectrogramGrid& grid)
{
    if (! grid.valid()) return {};
    juce::Image image(juce::Image::ARGB, grid.columns, grid.rows, true);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    const auto lut = buildLut(grid.floorDb);
    const auto span = (float) (0.0 - grid.floorDb);
    for (int row = 0; row < grid.rows; ++row)
    {
        // Row 0 is the lowest frequency and lives at the bottom of the card.
        auto* line = reinterpret_cast<juce::PixelARGB*>(data.getLinePointer(grid.rows - 1 - row));
        for (int column = 0; column < grid.columns; ++column)
        {
            const auto level = span > 0.0f ? (grid.at(row, column) - (float) grid.floorDb) / span : 0.0f;
            const auto index = juce::jlimit(0, lutSize - 1, (int) std::lround(level * (float) (lutSize - 1)));
            line[column] = lut[(size_t) index];
        }
    }
    return image;
}

void drawFrequencyLabels(juce::Graphics& g, juce::Rectangle<float> area, const SpectrogramGrid& grid,
                         juce::Rectangle<float> avoid)
{
    if (! grid.valid() || area.getHeight() <= 0.0f) return;
    g.setFont(fonts::font(10));
    const double guides[] = { 100.0, 1000.0, 10000.0 };
    for (const auto frequency : guides)
    {
        const auto position = spectrogramFrequencyPosition(grid, frequency);
        if (position < 0.03 || position > 0.95) continue;
        const auto y = area.getBottom() - (float) position * area.getHeight();
        g.setColour(theme::line.withAlpha(0.35f));
        g.drawHorizontalLine((int) y, area.getX(), area.getRight());
        const auto text = frequency >= 1000.0 ? juce::String(frequency / 1000.0, 0) + "k"
                                              : juce::String((int) frequency);
        juce::Rectangle<float> chip(area.getX() + 3.0f, y - 6.0f, 26.0f, 12.0f);
        if (avoid.getWidth() > 0.0f && avoid.intersects(chip))
            chip.setX(avoid.getRight() + 5.0f);
        g.setColour(theme::panel.withAlpha(0.72f));
        g.fillRoundedRectangle(chip, 3.0f);
        g.setColour(theme::muted);
        g.drawText(text, chip, juce::Justification::centred);
    }
}

void drawOverviewNote(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setFont(fonts::font(10));
    const juce::Rectangle<float> chip(area.getRight() - 62.0f, area.getBottom() - 17.0f, 59.0f, 13.0f);
    g.setColour(theme::panel.withAlpha(0.72f));
    g.fillRoundedRectangle(chip, 3.0f);
    g.setColour(theme::muted);
    g.drawText("overview", chip, juce::Justification::centred);
}
} // namespace sonicator::spectrogramview
