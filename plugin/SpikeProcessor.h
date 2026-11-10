#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "core/audio/AudioEngine.h"

namespace sonicator::spike {
// Technical spike only: a silent instrument whose editor hosts the existing MainComponent.
// It answers whether the shared UI and core libraries build, sign and load inside a DAW host.
class SpikeProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* productName = "Sonicator Spike";
    // The engine lives in the processor so preview audio and host MIDI survive editor closes.
    sonicator::AudioEngine& engine() { return hostEngine; }

    SpikeProcessor();
    ~SpikeProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

private:
    sonicator::AudioEngine hostEngine { false };
    // Hosts that show both the parameter list and generic controls need a tree; the spike has
    // no parameters yet.
    juce::AudioProcessorValueTreeState parameters { *this, nullptr, "Parameters", {} };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpikeProcessor)
};
} // namespace sonicator::spike
