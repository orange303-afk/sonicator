#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "core/audio/AudioEngine.h"
#include "plugin/PluginState.h"

namespace sonicator::plugin
{
// The instrument wrapper around the shared library browser. It owns the host-driven audio engine
// (no device is ever opened) and the state a DAW project stores; the editor owns MainComponent.
class PluginProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* productName = "SONICATOR";

    // The host wrapper uses the shared application catalog and settings. Tests pass a temporary
    // catalog and settings file so they never touch the owner's library.
    PluginProcessor();
    PluginProcessor(juce::File databaseFile, juce::File settingsFile);

    juce::File databaseFile() const;
    juce::File settingsFile() const;

    // A copy, so a host callback never reads a half-written state.
    PluginState pluginState() const;
    void setPluginState(const PluginState&);
    void updateSession(const juce::String& folder, const juce::String& sample);
    void updateWindowSize(int width, int height);

    // The engine lives in the processor so preview audio and host MIDI survive editor closes.
    sonicator::AudioEngine& engine() { return hostEngine; }

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
    // Hosts that show both the parameter list and generic controls need a tree; the browser has
    // no automatable parameters yet.
    juce::AudioProcessorValueTreeState parameters { *this, nullptr, "Parameters", {} };
    juce::File database, settings;
    mutable juce::CriticalSection stateLock;
    PluginState state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
} // namespace sonicator::plugin
