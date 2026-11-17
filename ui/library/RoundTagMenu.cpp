#include "RoundTagMenu.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace sonicator
{
namespace
{
constexpr float innerRadius = 122.0f;
constexpr float outerRadius = 200.0f;
constexpr float pillHeight = 26.0f;
constexpr float pillRadius = 13.0f;
constexpr float hubSize = 84.0f;
constexpr float edgeMargin = 12.0f;
constexpr float dotSize = 7.0f;
constexpr float dotInset = 10.0f;
constexpr float dotGap = 6.0f;
constexpr float textInset = 9.0f;
constexpr float checkRoom = 10.0f;
constexpr float nearestReach = 90.0f;
constexpr float maxGrowth = 0.26f;
constexpr float pushDistance = 13.0f;
constexpr double growthTimeConstant = 0.18;

const juce::String checkMark = juce::String::charToString((juce::juce_wchar) 0x2713);

float pillWidth(const RoundTagItem& item, const juce::Font& font)
{
    const auto text = item.label + (item.assigned ? " " + checkMark : juce::String());
    return dotInset + dotSize + dotGap
         + (float) juce::GlyphArrangement::getStringWidthInt(font, text) + textInset;
}

float distanceToRect(const juce::Rectangle<float>& r, juce::Point<float> p)
{
    const float dx = juce::jmax(juce::jmax(r.getX() - p.x, 0.0f), p.x - r.getRight());
    const float dy = juce::jmax(juce::jmax(r.getY() - p.y, 0.0f), p.y - r.getBottom());
    return std::sqrt(dx * dx + dy * dy);
}

float easeOutCubic(float t)
{
    const float inv = 1.0f - juce::jlimit(0.0f, 1.0f, t);
    return 1.0f - inv * inv * inv;
}
}

RoundTagGeometry layoutRoundTagMenu(juce::Point<int> anchor,
                                    juce::Rectangle<int> available,
                                    const std::vector<RoundTagItem>& inner,
                                    const std::vector<RoundTagItem>& outer,
                                    const juce::Font& font)
{
    RoundTagGeometry geometry;
    geometry.centre = anchor.toFloat();
    geometry.hub = juce::Rectangle<float>(hubSize, hubSize).withCentre(geometry.centre);

    auto addRing = [&](const std::vector<RoundTagItem>& items, int ringIndex, float radius,
                       float start)
    {
        const int count = (int) items.size();
        if (count <= 0) return;
        const float step = juce::MathConstants<float>::twoPi / (float) count;
        for (int i = 0; i < count; ++i)
        {
            const float angle = start + step * (float) i;
            RoundTagPill pill;
            pill.item = items[(size_t) i];
            pill.angle = angle;
            pill.ring = ringIndex;
            pill.slot = i;
            pill.centre = geometry.centre
                + juce::Point<float> { std::cos(angle), std::sin(angle) } * radius;
            pill.bounds = juce::Rectangle<float>(pillWidth(pill.item, font), pillHeight)
                              .withCentre(pill.centre);
            geometry.pills.push_back(pill);
        }
    };
    addRing(inner, 0, innerRadius, -juce::MathConstants<float>::halfPi);
    if (! outer.empty())
        addRing(outer, 1, outerRadius,
                -juce::MathConstants<float>::halfPi
                    + juce::MathConstants<float>::pi / (float) outer.size());

    auto united = geometry.hub;
    for (const auto& pill : geometry.pills) united = united.getUnion(pill.bounds);
    // Hover grows the pill and pushes it outward; reserve that room too, or an edge pill still
    // clips after the base rectangle has been clamped.
    float reserveX = 0.0f, reserveY = 0.0f;
    for (const auto& pill : geometry.pills)
    {
        reserveX = juce::jmax(reserveX, pushDistance + maxGrowth * pill.bounds.getWidth() * 0.5f);
        reserveY = juce::jmax(reserveY, pushDistance + maxGrowth * pill.bounds.getHeight() * 0.5f);
    }
    const auto unitedGrown = united.expanded(reserveX, reserveY);
    const auto area = available.toFloat().reduced(edgeMargin);
    float dx = 0.0f, dy = 0.0f;
    if (unitedGrown.getWidth() <= area.getWidth())
    {
        if (unitedGrown.getX() < area.getX()) dx = area.getX() - unitedGrown.getX();
        if (unitedGrown.getRight() > area.getRight()) dx = area.getRight() - unitedGrown.getRight();
    }
    else
    {
        dx = area.getCentreX() - unitedGrown.getCentreX();
    }
    if (unitedGrown.getHeight() <= area.getHeight())
    {
        if (unitedGrown.getY() < area.getY()) dy = area.getY() - unitedGrown.getY();
        if (unitedGrown.getBottom() > area.getBottom()) dy = area.getBottom() - unitedGrown.getBottom();
    }
    else
    {
        dy = area.getCentreY() - unitedGrown.getCentreY();
    }
    if (dx != 0.0f || dy != 0.0f)
    {
        geometry.centre += { dx, dy };
        geometry.hub = geometry.hub.translated(dx, dy);
        for (auto& pill : geometry.pills)
        {
            pill.centre += { dx, dy };
            pill.bounds = pill.bounds.translated(dx, dy);
        }
    }
    return geometry;
}

int roundTagNearest(const std::vector<RoundTagPill>& pills, juce::Point<float> mouse, float reach)
{
    int best = -1;
    float bestDistance = reach;
    for (size_t i = 0; i < pills.size(); ++i)
    {
        const float d = distanceToRect(pills[i].bounds, mouse);
        if (d <= bestDistance)
        {
            bestDistance = d;
            best = (int) i;
        }
    }
    return best;
}

RoundTagMenu::RoundTagMenu()
{
    setOpaque(false);
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    setVisible(false);
}

RoundTagMenu::~RoundTagMenu() = default;

void RoundTagMenu::open(RoundTagGeometry nextGeometry, juce::String centreTitle,
                        juce::String centreCaption)
{
    geometry = std::move(nextGeometry);
    title = std::move(centreTitle);
    caption = std::move(centreCaption);
    growth.assign(geometry.pills.size(), 0.0f);
    target.assign(geometry.pills.size(), 0.0f);
    mouse = { -1.0e6f, -1.0e6f };
    previousFocus = juce::Component::getCurrentlyFocusedComponent();
    openFlag = true;
    closing = false;
    openProgress = animationMs <= 0 ? 1.0f : 0.0f;
    openStarted = juce::Time::getMillisecondCounterHiRes();
    lastTick = 0.0;
    setVisible(true);
    toFront(false);
    if (isShowing()) grabKeyboardFocus();
    startTimerHz(60);
    repaint();
}

void RoundTagMenu::close()
{
    if (! openFlag) return;
    if (animationMs <= 0)
    {
        hideNow();
        return;
    }
    closing = true;
    closeStarted = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

void RoundTagMenu::dismiss()
{
    if (! openFlag) return;
    close();
    if (onDismissed) onDismissed();
}

void RoundTagMenu::hideNow()
{
    openFlag = false;
    closing = false;
    openProgress = 0.0f;
    stopTimer();
    setVisible(false);
    // Keyboard focus goes back where it came from, so tagging then arrowing to the next row works.
    if (auto* previous = previousFocus.getComponent())
        if (previous->isShowing()) previous->grabKeyboardFocus();
    previousFocus = nullptr;
}

juce::Rectangle<float> RoundTagMenu::renderedBounds(int index) const
{
    const auto& pill = geometry.pills[(size_t) index];
    const float k = juce::jlimit(0.0f, 1.0f, growth[(size_t) index] / maxGrowth);
    const float push = pushDistance * k;
    auto r = pill.bounds.withCentre(
        pill.bounds.getCentre()
        + juce::Point<float> { std::cos(pill.angle), std::sin(pill.angle) } * push);
    const float scale = 1.0f + growth[(size_t) index];
    return r.withSizeKeepingCentre(r.getWidth() * scale, r.getHeight() * scale);
}

void RoundTagMenu::updateTargets()
{
    for (size_t i = 0; i < geometry.pills.size(); ++i)
    {
        const float distance = distanceToRect(geometry.pills[i].bounds, mouse);
        const float t = juce::jlimit(0.0f, 1.0f, 1.0f - distance / nearestReach);
        target[i] = (t * t * (3.0f - 2.0f * t)) * maxGrowth;
    }
}

void RoundTagMenu::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastTick > 0.0
        ? juce::jlimit(0.001, 0.05, (now - lastTick) / 1000.0) : 0.016;
    lastTick = now;

    if (closing)
    {
        openProgress = (float) juce::jlimit(0.0, 1.0,
            1.0 - (now - closeStarted) / 90.0);
        if (openProgress <= 0.0f)
        {
            hideNow();
        }
    }
    else if (openProgress < 1.0f)
    {
        openProgress = animationMs <= 0 ? 1.0f
            : (float) juce::jlimit(0.0, 1.0, (now - openStarted) / (double) animationMs);
    }

    const float alpha = 1.0f - std::exp((float) (-dt / growthTimeConstant));
    updateTargets();
    for (size_t i = 0; i < growth.size(); ++i)
        growth[i] += (target[i] - growth[i]) * alpha;
    repaint();
}

void RoundTagMenu::paint(juce::Graphics& g)
{
    if (openProgress <= 0.0f || geometry.pills.empty()) return;
    const float visual = juce::jlimit(0.0f, 1.0f, openProgress);
    const float eased = easeOutCubic(visual);
    g.beginTransparencyLayer(eased);
    g.addTransform(juce::AffineTransform::translation(-geometry.centre.x, -geometry.centre.y)
                       .scaled(0.86f + 0.14f * eased)
                       .translated(geometry.centre.x, geometry.centre.y));

    // A soft local shade under the ring only; the rest of the window stays readable.
    for (int i = 4; i >= 1; --i)
    {
        const float radius = 120.0f + (float) i * 42.0f;
        g.setColour(juce::Colours::black.withAlpha(0.055f));
        g.fillEllipse(geometry.centre.x - radius, geometry.centre.y - radius,
                      radius * 2.0f, radius * 2.0f);
    }

    std::vector<int> order((size_t) geometry.pills.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [this](int a, int b) { return growth[(size_t) a] < growth[(size_t) b]; });

    for (const int index : order)
    {
        const auto& pill = geometry.pills[(size_t) index];
        const float k = juce::jlimit(0.0f, 1.0f, growth[(size_t) index] / maxGrowth);
        const auto r = renderedBounds(index);
        const float scale = 1.0f + growth[(size_t) index];

        g.setColour(theme::panel.brighter(0.10f * k));
        g.fillRoundedRectangle(r, pillRadius * scale);

        const auto border = pill.item.action
            ? theme::muted.withAlpha(0.35f + 0.45f * k)
            : pill.item.colour.withAlpha(0.45f + 0.50f * k);
        juce::Path outline;
        outline.addRoundedRectangle(r, pillRadius * scale);
        if (pill.item.action)
        {
            juce::Path dashed;
            const float pattern[] = { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, pattern, 2);
            g.setColour(border);
            g.fillPath(dashed);
        }
        else
        {
            g.setColour(border);
            g.strokePath(outline, juce::PathStrokeType(1.0f));
        }

        const float dotRadius = (dotSize * 0.5f) * scale;
        const float dotX = r.getX() + (dotInset * scale) + dotRadius;
        g.setColour(pill.item.action ? theme::muted
                                     : pill.item.colour.brighter(0.15f));
        g.fillEllipse(dotX - dotRadius, r.getCentreY() - dotRadius,
                      dotRadius * 2.0f, dotRadius * 2.0f);

        g.setColour(pill.item.action ? theme::muted : theme::text);
        g.setFont(fonts::font(10.5f * scale, fonts::Weight::medium));
        const auto label = pill.item.label
            + (pill.item.assigned && ! pill.item.action ? " " + checkMark : juce::String());
        auto textArea = r;
        textArea.removeFromLeft((dotInset + dotSize + dotGap) * scale);
        textArea.removeFromRight(4.0f * scale);
        g.drawText(label, textArea, juce::Justification::centredLeft, false);
    }

    g.setColour(theme::panel.withAlpha(0.94f));
    g.fillEllipse(geometry.hub);
    g.setColour(theme::line);
    g.drawEllipse(geometry.hub, 1.0f);
    auto titleArea = geometry.hub.withSizeKeepingCentre(geometry.hub.getWidth(), 18.0f)
                                        .translated(0.0f, -8.0f);
    auto captionArea = geometry.hub.withSizeKeepingCentre(geometry.hub.getWidth(), 12.0f)
                                          .translated(0.0f, 9.0f);
    g.setColour(theme::text);
    g.setFont(fonts::font(12.5f, fonts::Weight::semiBold));
    g.drawText(title, titleArea, juce::Justification::centred, false);
    g.setColour(theme::muted);
    g.setFont(fonts::font(8.5f).withExtraKerningFactor(0.12f));
    g.drawText(caption, captionArea, juce::Justification::centred, false);

    g.endTransparencyLayer();
}

void RoundTagMenu::mouseMove(const juce::MouseEvent& event)
{
    mouse = event.position;
}

void RoundTagMenu::mouseDown(const juce::MouseEvent& event)
{
    if (! openFlag || closing) return;
    if (event.mods.isPopupMenu())
    {
        dismiss();
        return;
    }
    // Paint draws the most grown pill last, so the hit test must use the same order.
    std::vector<int> order((size_t) geometry.pills.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [this](int a, int b) { return growth[(size_t) a] < growth[(size_t) b]; });
    int picked = -1;
    for (const int index : order)
        if (renderedBounds(index).contains(event.position)) picked = index;
    if (picked < 0)
    {
        dismiss();
        return;
    }
    const auto item = geometry.pills[(size_t) picked].item;
    close();
    if (item.action)
    {
        if (onAllTagsPicked) onAllTagsPicked();
    }
    else if (onTagPicked)
    {
        onTagPicked(item.tagId);
    }
}

bool RoundTagMenu::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress(juce::KeyPress::escapeKey))
    {
        dismiss();
        return true;
    }
    return false;
}
} // namespace sonicator
