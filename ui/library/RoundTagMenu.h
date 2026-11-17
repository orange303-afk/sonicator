#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace sonicator
{
// One pill in the round tag menu. tagId == 0 with action == true is the "All tags…" entry.
struct RoundTagItem
{
    int64_t tagId = 0;
    juce::String label;
    juce::Colour colour;
    bool assigned = false;
    bool action = false;
};

struct RoundTagPill
{
    RoundTagItem item;
    juce::Rectangle<float> bounds; // un-transformed pill rectangle
    juce::Point<float> centre;
    float angle = 0.0f;            // radians from the ring centre, 0 = right
    int ring = 0, slot = 0;
};

struct RoundTagGeometry
{
    juce::Point<float> centre;
    juce::Rectangle<float> hub;
    std::vector<RoundTagPill> pills;
};

// Two orbits, eight slots each at most: inner radius 122, outer 200. A short list is spread
// evenly instead of leaving a gap. The ring is shifted so every pill stays inside `available`
// with a 12 px margin; when it cannot fit, it is centred.
RoundTagGeometry layoutRoundTagMenu(juce::Point<int> anchor,
                                    juce::Rectangle<int> available,
                                    const std::vector<RoundTagItem>& inner,
                                    const std::vector<RoundTagItem>& outer,
                                    const juce::Font& font);

// Index of the pill whose rectangle is nearest the point, or -1 when the nearest is farther
// than `reach`. Reach 0 means "only inside a pill".
int roundTagNearest(const std::vector<RoundTagPill>& pills, juce::Point<float> mouse,
                    float reach = 90.0f);

// Transparent full-window overlay. It appears around a point, grows the pill the cursor is
// approaching, and reports one pick. The owner adds it as a child and sizes it to the window.
class RoundTagMenu final : public juce::Component, private juce::Timer
{
public:
    RoundTagMenu();
    ~RoundTagMenu() override;

    void open(RoundTagGeometry geometry, juce::String centreTitle, juce::String centreCaption);
    void close();
    bool isOpen() const noexcept { return openFlag; }

    // 0 disables the appearance transition for tests and manual plates.
    void setAnimationDuration(int milliseconds) { animationMs = juce::jmax(0, milliseconds); }

    std::function<void(int64_t tagId)> onTagPicked;
    std::function<void()> onAllTagsPicked;
    std::function<void()> onDismissed;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void dismiss();
    void updateTargets();
    juce::Rectangle<float> renderedBounds(int index) const;

    RoundTagGeometry geometry;
    juce::String title, caption;
    std::vector<float> growth, target;
    juce::Point<float> mouse { -1.0e6f, -1.0e6f };
    double openStarted = 0.0, closeStarted = 0.0, lastTick = 0.0;
    float openProgress = 0.0f;
    int animationMs = 260;
    bool openFlag = false, closing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoundTagMenu)
};
} // namespace sonicator
