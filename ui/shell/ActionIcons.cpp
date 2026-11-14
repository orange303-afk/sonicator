#include "ActionIcons.h"

namespace sonicator
{
namespace
{
struct IconShape
{
    juce::Path path;
    bool filled = false;
};

IconShape makeIcon(ActionIcon icon)
{
    juce::Path p;
    switch (icon)
    {
        case ActionIcon::settings:
        {
            // A hub with eight spokes reads as a gear at 16 px without the visual noise of teeth.
            const float cx = 0.5f, cy = 0.5f;
            p.addEllipse(cx - 0.15f, cy - 0.15f, 0.30f, 0.30f);
            for (int i = 0; i < 8; ++i)
            {
                const float angle = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                p.startNewSubPath(cx + 0.22f * std::cos(angle), cy + 0.22f * std::sin(angle));
                p.lineTo(cx + 0.38f * std::cos(angle), cy + 0.38f * std::sin(angle));
            }
            return { p, false };
        }
        case ActionIcon::search:
        {
            p.addEllipse(0.14f, 0.14f, 0.46f, 0.46f);
            p.startNewSubPath(0.60f, 0.60f);
            p.lineTo(0.86f, 0.86f);
            return { p, false };
        }
        case ActionIcon::play:
            p.addTriangle(0.28f, 0.18f, 0.28f, 0.82f, 0.82f, 0.5f);
            return { p, true };
        case ActionIcon::pause:
            p.addRectangle(0.30f, 0.18f, 0.13f, 0.64f);
            p.addRectangle(0.57f, 0.18f, 0.13f, 0.64f);
            return { p, true };
        case ActionIcon::stop:
            p.addRectangle(0.26f, 0.26f, 0.48f, 0.48f);
            return { p, true };
        case ActionIcon::folder:
        {
            p.startNewSubPath(0.10f, 0.30f);
            p.lineTo(0.10f, 0.76f);
            p.lineTo(0.90f, 0.76f);
            p.lineTo(0.90f, 0.36f);
            p.lineTo(0.46f, 0.36f);
            p.lineTo(0.38f, 0.26f);
            p.lineTo(0.10f, 0.26f);
            p.closeSubPath();
            return { p, false };
        }
        case ActionIcon::favorite:
        {
            const float cx = 0.5f, cy = 0.52f;
            for (int i = 0; i < 10; ++i)
            {
                const float radius = (i % 2 == 0) ? 0.38f : 0.16f;
                const float angle = -juce::MathConstants<float>::halfPi
                    + juce::MathConstants<float>::twoPi * (float) i / 10.0f;
                const float x = cx + radius * std::cos(angle);
                const float y = cy + radius * std::sin(angle);
                if (i == 0) p.startNewSubPath(x, y);
                else p.lineTo(x, y);
            }
            p.closeSubPath();
            return { p, false };
        }
        case ActionIcon::reset:
        {
            // Three-quarter arc with an arrow head: the usual "restore defaults" sign.
            p.addCentredArc(0.5f, 0.52f, 0.32f, 0.32f, 0.0f,
                            -juce::MathConstants<float>::halfPi,
                            juce::MathConstants<float>::pi * 1.35f, false);
            p.startNewSubPath(0.5f, 0.14f);
            p.lineTo(0.34f, 0.24f);
            p.lineTo(0.5f, 0.34f);
            return { p, false };
        }
        case ActionIcon::close:
            p.startNewSubPath(0.26f, 0.26f); p.lineTo(0.74f, 0.74f);
            p.startNewSubPath(0.74f, 0.26f); p.lineTo(0.26f, 0.74f);
            return { p, false };
        case ActionIcon::add:
            p.startNewSubPath(0.5f, 0.24f); p.lineTo(0.5f, 0.76f);
            p.startNewSubPath(0.24f, 0.5f); p.lineTo(0.76f, 0.5f);
            return { p, false };
        case ActionIcon::remove:
            p.startNewSubPath(0.24f, 0.5f); p.lineTo(0.76f, 0.5f);
            return { p, false };
        case ActionIcon::chevronRight:
            p.startNewSubPath(0.40f, 0.28f); p.lineTo(0.62f, 0.5f); p.lineTo(0.40f, 0.72f);
            return { p, false };
        case ActionIcon::chevronDown:
            p.startNewSubPath(0.28f, 0.42f); p.lineTo(0.5f, 0.62f); p.lineTo(0.72f, 0.42f);
            return { p, false };
    }
    return { p, false };
}
} // namespace

juce::Path actionIconPath(ActionIcon icon)
{
    return makeIcon(icon).path;
}

void drawActionIcon(juce::Graphics& g, ActionIcon icon, juce::Rectangle<float> bounds, float strokeWidth)
{
    const auto shape = makeIcon(icon);
    if (shape.path.isEmpty()) return;
    const auto target = juce::Rectangle<float>(0.0f, 0.0f, 1.0f, 1.0f);
    const auto scaled = shape.path.getTransformToScaleToFit(bounds, true);
    juce::ignoreUnused(target);
    if (shape.filled)
    {
        g.fillPath(shape.path, scaled);
        return;
    }
    g.strokePath(shape.path, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded), scaled);
}
} // namespace sonicator
