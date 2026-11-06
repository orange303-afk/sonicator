#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
namespace sonicator {
class MainComponent final : public juce::Component
{
public:
    MainComponent() { setSize(1280, 800); }
    void paint(juce::Graphics& g) override;
};
}
