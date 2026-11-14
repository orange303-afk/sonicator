#pragma once
#include <juce_graphics/juce_graphics.h>

namespace sonicator::fonts
{
// Manrope ships inside the application: the interface must look the same on every machine, and
// the system sans differs between macOS, Windows and Linux. The four faces carry Cyrillic.
enum class Weight { regular, medium, semiBold, bold };

// Manrope's ascent and descent box is taller than the system sans, and JUCE sizes a font by that
// box: at the same height Manrope draws about 9% narrower and smaller, so this first factor
// restores the optical size the layouts were measured with. The second is the owner's request for
// a tenth more text on top of that.
inline constexpr float parityWithSystemSans = 1.09f;
inline constexpr float requestedTextScale = 1.10f;
inline constexpr float heightCalibration = parityWithSystemSans * requestedTextScale;

// Registers the embedded faces and makes Manrope the default sans-serif, so every
// juce::FontOptions(size) written elsewhere resolves to it. Safe to call more than once; the
// application calls it before it builds a window, the tests before they build a component.
void install();

// A font in Manrope. The weight selects one of the embedded faces instead of asking the system
// for a bold synthesis, so headings keep their drawn weight.
juce::FontOptions options(float height, Weight = Weight::regular);
juce::Font font(float height, Weight = Weight::regular);

// The face for a weight, for a component that paints with Graphics::setFont.
juce::Typeface::Ptr typeface(Weight);
}
