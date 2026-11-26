#pragma once
#include <juce_graphics/juce_graphics.h>
#include "core/dsp/Spectrogram.h"

namespace sonicator::spectrogramview
{
// One ARGB image at grid resolution. The card scales it to its bounds, so a resize never needs
// a new analysis unless the column/row budget changed.
juce::Image render(const SpectrogramGrid&);

juce::Colour colourForDb(float db, double floorDb);

// Frequency guides (100 Hz, 1 kHz, 10 kHz) drawn as an overlay inside the card. Deliberately not
// a reserved gutter: the mouse mapping uses the full card width, exactly as the waveform does.
// A label chip that would fall inside `avoid` (the selection toolbar) moves to its right.
void drawFrequencyLabels(juce::Graphics&, juce::Rectangle<float> area, const SpectrogramGrid&,
                         juce::Rectangle<float> avoid = {});

// Bottom-right note when the analysis had to aggregate time (a long file seen whole).
void drawOverviewNote(juce::Graphics&, juce::Rectangle<float> area);
} // namespace sonicator::spectrogramview
