#include "plugin/SpikeProcessor.h"
#include "plugin/SpikeEditor.h"

namespace sonicator::spike {
SpikeProcessor::SpikeProcessor()
{
    // Instrument layout: no audio inputs, one stereo output, MIDI input announced by the wrapper.
    setPlayConfigDetails(0, 2, 44100.0, 512);
}

void SpikeProcessor::prepareToPlay(double, int) {}

void SpikeProcessor::releaseResources() {}

void SpikeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* SpikeProcessor::createEditor()
{
    return new SpikeEditor(*this);
}

bool SpikeProcessor::hasEditor() const { return true; }

const juce::String SpikeProcessor::getName() const { return productName; }

bool SpikeProcessor::acceptsMidi() const { return true; }

bool SpikeProcessor::producesMidi() const { return false; }

bool SpikeProcessor::isMidiEffect() const { return false; }

double SpikeProcessor::getTailLengthSeconds() const { return 0.0; }

int SpikeProcessor::getNumPrograms() { return 1; }

int SpikeProcessor::getCurrentProgram() { return 0; }

void SpikeProcessor::setCurrentProgram(int) {}

const juce::String SpikeProcessor::getProgramName(int) { return productName; }

void SpikeProcessor::changeProgramName(int, const juce::String&) {}

void SpikeProcessor::getStateInformation(juce::MemoryBlock&) {}

void SpikeProcessor::setStateInformation(const void*, int) {}

bool SpikeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}
} // namespace sonicator::spike

// The wrapper instantiates the processor through this factory.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sonicator::spike::SpikeProcessor();
}
