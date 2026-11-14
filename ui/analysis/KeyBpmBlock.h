#pragma once
#include "ui/shell/DesignTokens.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonicator
{
// The two numbers a person looks for first, shown large in the Analysis panel's free space:
// the key and the tempo, side by side, each with a small caption under it.
class KeyBpmBlock final : public juce::Component
{
public:
    KeyBpmBlock();
    void setValues(const juce::String& key, const juce::String& bpm);
    const juce::String& key() const noexcept { return keyValue; }
    const juce::String& bpm() const noexcept { return bpmValue; }
    static int preferredHeight();
    void paint(juce::Graphics&) override;

private:
    juce::String keyValue, bpmValue;
};
} // namespace sonicator
