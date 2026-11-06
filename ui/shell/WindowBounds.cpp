#include "WindowBounds.h"
namespace sonicator {
juce::Rectangle<int> resolveWindowBounds(juce::Rectangle<int> saved,
                                         juce::Rectangle<int> available,
                                         int minWidth,
                                         int minHeight)
{
    if (available.getWidth() < 200 || available.getHeight() < 150)
        available = { 0, 0, 1280, 800 };
    const int maxWidth = juce::jmax(240, available.getWidth());
    const int maxHeight = juce::jmax(180, available.getHeight());
    const int lowerWidth = juce::jmin(minWidth, maxWidth);
    const int lowerHeight = juce::jmin(minHeight, maxHeight);

    const bool hasSaved = saved.getWidth() > 0 && saved.getHeight() > 0;
    const bool visible = hasSaved
        && saved.intersects(available)
        && saved.getIntersection(available).getWidth() >= 80
        && saved.getIntersection(available).getHeight() >= 40;
    const int width = juce::jlimit(lowerWidth, maxWidth, hasSaved ? saved.getWidth() : 1280);
    const int height = juce::jlimit(lowerHeight, maxHeight, hasSaved ? saved.getHeight() : 800);
    int x = visible ? saved.getX() : available.getX() + (available.getWidth() - width) / 2;
    int y = visible ? saved.getY() : available.getY() + (available.getHeight() - height) / 2;
    x = juce::jlimit(available.getX(), juce::jmax(available.getX(), available.getRight() - width), x);
    y = juce::jlimit(available.getY(), juce::jmax(available.getY(), available.getBottom() - height), y);
    return { x, y, width, height };
}
juce::Rectangle<int> resolveWindowBounds(const AppSettings& settings, const juce::Rectangle<int>& available)
{
    return resolveWindowBounds({ settings.windowX, settings.windowY,
                                 settings.windowWidth, settings.windowHeight }, available);
}
} // namespace sonicator
