#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator::studio
{
// A paragraph of interface text. juce::Label fits as many lines as its height allows and squeezes
// the rest horizontally; this component measures the text it was given, so a longer note wraps
// into a new line instead of clipping or shrinking. Leading is set in pixels, which is what keeps
// a dense block of credits readable.
class Paragraph final : public juce::Component
{
public:
    Paragraph()
    {
        // Painted text must not swallow the tooltip the card or the row already owns.
        setInterceptsMouseClicks(false, false);
    }

    void setText(const juce::String&, juce::Font, juce::Colour, int extraLineSpacing = 4);
    void setText(const juce::String&);
    void setTextColour(juce::Colour);
    const juce::String& getText() const noexcept { return text; }
    juce::Colour getTextColour() const noexcept { return colour; }

    // The height this text needs at that width; the owner uses it when it lays the page out.
    int heightForWidth(int width) const;
    void paint(juce::Graphics&) override;

private:
    juce::TextLayout build(int width) const;

    juce::String text;
    juce::Font font { juce::FontOptions(12.5f) };
    juce::Colour colour { 0xffeeeef0 };
    int lineSpacing = 4;
};
} // namespace sonicator::studio
