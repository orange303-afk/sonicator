#pragma once
#include "ui/shell/StudioControls.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace sonicator
{
// What the Analysis panel shows for the current selection. The owner formats the values; the card
// only lays them out, so no formatting rule lives in two places.
struct AnalysisContent
{
    juce::String title, meta, tooltip;
    std::vector<studio::Stat> stats;
    std::vector<juce::String> notes;
};

// The value + caption grid that replaced the single padded-spaces label.
class AnalysisCard final : public juce::Component, public juce::SettableTooltipClient
{
public:
    AnalysisCard();
    void setContent(const AnalysisContent&);
    void showPlaceholder(const juce::String&);
    const AnalysisContent& content() const noexcept { return data; }
    // The height the content needs at that width, so the viewport scrolls instead of clipping.
    int preferredHeight(int width) const;
    // Two columns from 240 px up; one column keeps the values readable in a narrow panel.
    static int columnsFor(int width);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    AnalysisContent data;
    juce::Label title, meta;
    std::vector<std::unique_ptr<studio::StatTile>> tiles;
    std::vector<std::unique_ptr<juce::Label>> notes;
    static int wrappedHeight(const juce::String&, const juce::Font&, int width);
};
} // namespace sonicator
