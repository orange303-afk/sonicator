#include "MainComponent.h"
namespace sonicator {
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10131b));
    g.setColour(juce::Colours::white);
    g.drawText("SONICATOR", getLocalBounds(), juce::Justification::centred);
}
}
