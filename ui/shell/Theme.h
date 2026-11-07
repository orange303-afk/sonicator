#pragma once
#include "core/settings/AppSettings.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator::theme {
// Flat dark surfaces. No gradients, glows, shadows or translucent glass.
inline juce::Colour background { 0xff17181a }, panel { 0xff202124 }, raised { 0xff303236 };
inline juce::Colour hover { 0xff3a3c40 }, selection { 0xff484b50 };
inline juce::Colour table { 0xff131416 }, rowAlternate { 0xff191a1d };
inline juce::Colour text { 0xffeeeef0 }, muted { 0xffa3a5a9 };
inline juce::Colour line { 0xff34363a }, accent { 0xfff39a43 };
inline bool isDark = true;

inline bool systemPrefersDark()
{
    return juce::Desktop::getInstance().isDarkModeActive();
}
inline void apply(ThemeMode mode)
{
    isDark = mode == ThemeMode::Dark || (mode == ThemeMode::System && systemPrefersDark());
    if (isDark)
    {
        background = juce::Colour(0xff17181a); panel = juce::Colour(0xff202124); raised = juce::Colour(0xff303236);
        hover = juce::Colour(0xff3a3c40); selection = juce::Colour(0xff484b50);
        table = juce::Colour(0xff131416); rowAlternate = juce::Colour(0xff191a1d);
        text = juce::Colour(0xffeeeef0); muted = juce::Colour(0xffa3a5a9);
        line = juce::Colour(0xff34363a); accent = juce::Colour(0xfff39a43);
    }
    else
    {
        background = juce::Colour(0xffeef0f2); panel = juce::Colour(0xfff8f9fa); raised = juce::Colour(0xffe4e6e9);
        hover = juce::Colour(0xffd9dbdf); selection = juce::Colour(0xffcfd2d7);
        table = juce::Colour(0xfffbfbfc); rowAlternate = juce::Colour(0xfff3f4f6);
        text = juce::Colour(0xff1a1b1d); muted = juce::Colour(0xff63666a);
        line = juce::Colour(0xffd4d7db); accent = juce::Colour(0xffc2611c);
    }
}
// Tag colours are chosen for dots and borders; their label needs extra contrast on light panels.
inline juce::Colour tagText(juce::Colour tagColour)
{
    return isDark ? tagColour.brighter(0.55f) : tagColour.darker(0.42f);
}
inline juce::Colour disabled(juce::Colour colour)
{
    return colour.interpolatedWith(background, 0.55f);
}
}
namespace sonicator {
class SonicatorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SonicatorLookAndFeel() { refreshColours(); }

    void refreshColours()
    {
        setColour(juce::ResizableWindow::backgroundColourId,theme::background);
        setColour(juce::TextButton::buttonColourId,theme::raised);
        setColour(juce::TextButton::buttonOnColourId,theme::selection);
        setColour(juce::TextButton::textColourOffId,theme::text);
        setColour(juce::TextButton::textColourOnId,theme::text);
        setColour(juce::Label::textColourId,theme::text);
        setColour(juce::TextEditor::backgroundColourId,theme::panel);
        setColour(juce::TextEditor::textColourId,theme::text);
        setColour(juce::TextEditor::outlineColourId,theme::line);
        setColour(juce::TextEditor::focusedOutlineColourId,theme::line.brighter(theme::isDark ? 0.55f : 0.2f));
        setColour(juce::TextEditor::highlightColourId,theme::accent.withAlpha(0.25f));
        setColour(juce::TextEditor::highlightedTextColourId,theme::text);
        setColour(juce::CaretComponent::caretColourId,theme::text);
        setColour(juce::Slider::thumbColourId,theme::text);
        setColour(juce::Slider::trackColourId,theme::muted);
        setColour(juce::Slider::backgroundColourId,theme::line);
        setColour(juce::ToggleButton::textColourId,theme::text);
        setColour(juce::ToggleButton::tickColourId,theme::text);
        setColour(juce::ToggleButton::tickDisabledColourId,theme::line);
        setColour(juce::TableHeaderComponent::backgroundColourId,theme::table);
        setColour(juce::TableHeaderComponent::textColourId,theme::muted);
        setColour(juce::TableHeaderComponent::outlineColourId,theme::line);
        setColour(juce::TableHeaderComponent::highlightColourId,theme::hover);
        setColour(juce::ListBox::backgroundColourId,theme::table);
        setColour(juce::ListBox::outlineColourId,juce::Colours::transparentBlack);
        setColour(juce::ListBox::textColourId,theme::text);
        setColour(juce::TreeView::backgroundColourId,theme::panel);
        setColour(juce::TreeView::linesColourId,theme::line);
        setColour(juce::TreeView::selectedItemBackgroundColourId,theme::selection);
        setColour(juce::PopupMenu::backgroundColourId,theme::panel);
        setColour(juce::PopupMenu::textColourId,theme::text);
        setColour(juce::PopupMenu::headerTextColourId,theme::muted);
        setColour(juce::PopupMenu::highlightedBackgroundColourId,theme::selection);
        setColour(juce::PopupMenu::highlightedTextColourId,theme::text);
        setColour(juce::ComboBox::backgroundColourId,theme::panel);
        setColour(juce::ComboBox::textColourId,theme::text);
        setColour(juce::ComboBox::outlineColourId,theme::line);
        setColour(juce::ComboBox::arrowColourId,theme::muted);
        setColour(juce::ComboBox::focusedOutlineColourId,theme::line.brighter(theme::isDark ? 0.55f : 0.2f));
        setColour(juce::AlertWindow::backgroundColourId,theme::panel);
        setColour(juce::AlertWindow::textColourId,theme::text);
        setColour(juce::AlertWindow::outlineColourId,theme::line);
        setColour(juce::ScrollBar::thumbColourId,theme::raised);
        setColour(juce::ScrollBar::trackColourId,theme::background);
        setColour(juce::HyperlinkButton::textColourId,theme::accent);
        setColour(juce::TooltipWindow::backgroundColourId,theme::panel);
        setColour(juce::TooltipWindow::textColourId,theme::text);
        setColour(juce::TooltipWindow::outlineColourId,theme::line);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::Font(juce::FontOptions(13.0f));
    }

    juce::Font getAlertWindowTitleFont() override
    {
        return juce::Font(juce::FontOptions(16.0f, juce::Font::bold));
    }
    juce::Font getAlertWindowMessageFont() override
    {
        return juce::Font(juce::FontOptions(12.5f));
    }
    juce::Font getAlertWindowFont() override
    {
        return juce::Font(juce::FontOptions(12.0f));
    }
    int getAlertWindowButtonHeight() override { return 32; }

    class DocumentButton final : public juce::Button
    {
    public:
        explicit DocumentButton(int type) : juce::Button({}), buttonType(type)
        {
            setWantsKeyboardFocus(false);
            setMouseClickGrabsKeyboardFocus(false);
        }
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto area = getLocalBounds().toFloat().reduced(6.0f);
            const auto fill = buttonType == juce::DocumentWindow::closeButton
                ? theme::accent.withAlpha(down ? 0.65f : (highlighted ? 0.9f : 0.75f))
                : (highlighted || down ? theme::hover : theme::raised);
            g.setColour(fill);
            g.fillEllipse(area);
            g.setColour(buttonType == juce::DocumentWindow::closeButton ? theme::background : theme::muted);
            const auto glyph = area.reduced(4.5f);
            if (buttonType == juce::DocumentWindow::closeButton)
            {
                g.drawLine(glyph.getX(), glyph.getY(), glyph.getRight(), glyph.getBottom(), 1.2f);
                g.drawLine(glyph.getRight(), glyph.getY(), glyph.getX(), glyph.getBottom(), 1.2f);
            }
            else if (buttonType == juce::DocumentWindow::minimiseButton)
                g.drawLine(glyph.getX(), glyph.getCentreY(), glyph.getRight(), glyph.getCentreY(), 1.2f);
            else
                g.drawRect(glyph, 1.2f);
        }
    private:
        int buttonType;
    };
    juce::Button* createDocumentWindowButton(int buttonType) override
    {
        return new DocumentButton(buttonType);
    }
    void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g,
                                    int width, int height, int titleSpaceX, int titleSpaceW,
                                    const juce::Image* icon, bool drawTitleTextOnLeft) override
    {
        g.fillAll(theme::panel);
        g.setColour(theme::line);
        g.drawHorizontalLine(height - 1, 0.0f, (float) width);
        if (icon != nullptr && icon->isValid())
            g.drawImageWithin(*icon, 8, 0, height, height, juce::RectanglePlacement::centred);
        g.setColour(theme::text);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawFittedText(window.getName(), titleSpaceX, 0, titleSpaceW, height,
                         drawTitleTextOnLeft ? juce::Justification::centredLeft
                                             : juce::Justification::centred, 1);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool highlighted, bool down) override
    {
        auto base = button.getToggleState() ? theme::selection : theme::raised;
        if (down) base = theme::selection;
        else if (highlighted) base = button.getToggleState() ? theme::selection.brighter(0.08f) : theme::hover;
        if (! button.isEnabled()) base = theme::disabled(base);

        g.setColour(base);
        g.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(0.5f), 7.0f);

        if (button.hasKeyboardFocus(true) && button.isEnabled())
        {
            g.setColour(theme::muted.withAlpha(0.55f));
            g.drawRoundedRectangle(button.getLocalBounds().toFloat().reduced(1.5f), 6.0f, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.setColour(button.isEnabled() ? theme::text : theme::muted);
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(8, 0),
                         juce::Justification::centred, 1);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override
    {
        juce::ignoreUnused(down);
        const float box = 14.0f;
        const auto area = button.getLocalBounds().toFloat();
        const auto boxArea = juce::Rectangle<float>(area.getX() + 2.0f, area.getCentreY() - box / 2.0f, box, box);
        const auto on = button.getToggleState();

        g.setColour(on ? theme::text : theme::table);
        g.fillRoundedRectangle(boxArea, 3.0f);
        g.setColour(on ? theme::text : (highlighted ? theme::muted : theme::line));
        g.drawRoundedRectangle(boxArea.reduced(0.5f), 3.0f, 1.0f);

        if (on)
        {
            auto tick = getTickShape(0.62f);
            g.setColour(theme::table);
            g.fillPath(tick, tick.getTransformToScaleToFit(boxArea.reduced(3.0f, 3.5f), false));
        }

        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.setColour(button.isEnabled() ? theme::text : theme::muted);
        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().withTrimmedLeft(juce::roundToInt(boxArea.getRight()) + 6)
                             .withTrimmedRight(2),
                         juce::Justification::centredLeft, 1);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                   minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
        const float trackY = bounds.getCentreY();
        const float left = bounds.getX() + 5.0f, right = bounds.getRight() - 5.0f;
        const juce::Rectangle<float> track(left, trackY - 2.0f, juce::jmax(1.0f, right - left), 4.0f);

        g.setColour(theme::line);
        g.fillRoundedRectangle(track, 2.0f);
        g.setColour(slider.isEnabled() ? theme::muted : theme::disabled(theme::muted));
        g.fillRoundedRectangle(track.withRight(juce::jlimit(track.getX(), track.getRight(), sliderPos)), 2.0f);

        const float thumb = 10.0f;
        g.setColour(slider.isEnabled() ? theme::text : theme::muted);
        g.fillEllipse(sliderPos - thumb / 2.0f, trackY - thumb / 2.0f, thumb, thumb);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
        g.setColour(theme::panel);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(box.hasKeyboardFocus(false) ? theme::line.brighter(theme::isDark ? 0.55f : 0.2f) : theme::line);
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

        const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath(cx - 3.5f, cy - 1.8f);
        arrow.lineTo(cx, cy + 2.0f);
        arrow.lineTo(cx + 3.5f, cy - 1.8f);
        g.setColour(theme::muted);
        g.strokePath(arrow, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(12.5f));
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&) override
    {
        g.setColour(theme::panel);
        g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height), 7.0f);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        if (! editor.isEnabled()) return;
        const bool focused = editor.hasKeyboardFocus(true) && ! editor.isReadOnly();
        g.setColour(focused ? theme::line.brighter(theme::isDark ? 0.55f : 0.2f) : theme::line);
        g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f),
                               7.0f, 1.0f);
    }

    void drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) override
    {
        g.fillAll(theme::table);
        g.setColour(theme::line);
        g.drawHorizontalLine(header.getHeight() - 1, 0.0f, (float) header.getWidth());
    }

    void drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header,
                               const juce::String& columnName, int, int width, int height,
                               bool isMouseOver, bool isMouseDown, int columnFlags) override
    {
        if (isMouseDown) g.fillAll(theme::selection.withAlpha(0.5f));
        else if (isMouseOver) g.fillAll(theme::hover.withAlpha(0.4f));

        auto area = juce::Rectangle<int>(width, height).reduced(10, 0);
        if ((columnFlags & (juce::TableHeaderComponent::sortedForwards
                            | juce::TableHeaderComponent::sortedBackwards)) != 0)
        {
            juce::Path arrow;
            arrow.addTriangle(0.0f, 0.0f, 0.5f,
                              (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0 ? -0.8f : 0.8f,
                              1.0f, 0.0f);
            g.setColour(theme::muted);
            g.fillPath(arrow, arrow.getTransformToScaleToFit(
                area.removeFromRight(height / 2).reduced(2).toFloat(), true));
            area.removeFromRight(6);
        }

        g.setColour(header.findColour(juce::TableHeaderComponent::textColourId));
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawFittedText(columnName, area, juce::Justification::centredLeft, 1);
    }

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                       bool vertical, int thumbStart, int thumbSize, bool isMouseOver, bool) override
    {
        if (thumbSize <= 0) return;
        const int thickness = juce::jmin(6, vertical ? width : height);
        juce::Rectangle<int> thumb;
        if (vertical)
            thumb = { x + (width - thickness) / 2, thumbStart, thickness, thumbSize };
        else
            thumb = { thumbStart, y + (height - thickness) / 2, thumbSize, thickness };

        g.setColour(isMouseOver ? theme::selection : theme::raised);
        g.fillRoundedRectangle(thumb.toFloat(), (float) thickness / 2.0f);
    }
};
} // namespace sonicator
