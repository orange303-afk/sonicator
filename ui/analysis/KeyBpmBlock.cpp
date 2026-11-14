#include "KeyBpmBlock.h"
#include "ui/shell/StudioControls.h"
#include "ui/shell/Theme.h"

namespace sonicator
{
KeyBpmBlock::KeyBpmBlock()
{
    setComponentID("analysisKeyBpm");
}

void KeyBpmBlock::setValues(const juce::String& key, const juce::String& bpm)
{
    keyValue = key;
    bpmValue = bpm;
    // The block only exists when there is something to say: an unanalysed file gets no hero.
    // Smart metadata uses the same markers in this block as in the table: nothing to show yet.
    const auto known = [](const juce::String& text)
    {
        return text.isNotEmpty() && text != "--" && text != "?" && text != "??"
            && text != "n/a" && text != "-";
    };
    setVisible(known(keyValue) || known(bpmValue));
    setTitle("Key " + (keyValue.isEmpty() ? juce::String("unknown") : keyValue)
             + ", BPM " + (bpmValue.isEmpty() ? juce::String("unknown") : bpmValue));
    repaint();
}

int KeyBpmBlock::preferredHeight()
{
    return juce::roundToInt(studio::heroValueFont().getHeight())
         + 2 + juce::roundToInt(studio::captionFont().getHeight());
}

void KeyBpmBlock::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto column = [&g](juce::Rectangle<int> cell, const juce::String& value, const juce::String& caption)
    {
        g.setColour(theme::text);
        g.setFont(studio::heroValueFont());
        g.drawFittedText(value.isEmpty() ? juce::String("--") : value,
                         cell.removeFromTop(juce::roundToInt(studio::heroValueFont().getHeight())),
                         juce::Justification::centredLeft, 1);
        g.setColour(theme::muted);
        g.setFont(studio::captionFont());
        g.drawFittedText(caption, cell, juce::Justification::topLeft, 1);
    };
    const int half = area.getWidth() / 2;
    column(area.removeFromLeft(half), keyValue, "Key");
    column(area, bpmValue, "BPM");
}
} // namespace sonicator
