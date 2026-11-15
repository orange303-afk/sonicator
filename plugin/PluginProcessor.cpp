#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "core/settings/AppSettings.h"
#include "ui/MainComponent.h"

namespace sonicator::plugin
{
namespace
{
// The application may have moved its catalog; the plugin follows the same path the application
// would open, so both windows always show one library.
juce::File sharedDatabaseFile()
{
    const auto settings = SettingsStore(AppSettings::defaultSettingsFile()).load();
    if (settings.databasePath.isNotEmpty()) return juce::File(settings.databasePath);
    return MainComponent::defaultDatabaseFile();
}
} // namespace

PluginProcessor::PluginProcessor()
    : PluginProcessor(sharedDatabaseFile(), AppSettings::defaultSettingsFile())
{
}

PluginProcessor::PluginProcessor(juce::File databaseFile, juce::File settingsFile)
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(),
                                                        true)),
      database(std::move(databaseFile)), settings(std::move(settingsFile))
{
    // Instrument layout: no audio inputs, one stereo output, MIDI input announced by the wrapper.
    setPlayConfigDetails(0, 2, 44100.0, 512);
}

juce::File PluginProcessor::databaseFile() const { return database; }
juce::File PluginProcessor::settingsFile() const { return settings; }

PluginState PluginProcessor::pluginState() const
{
    const juce::ScopedLock lock(stateLock);
    return state;
}

void PluginProcessor::setPluginState(const PluginState& next)
{
    const juce::ScopedLock lock(stateLock);
    state = next;
    ++generation;
}

int PluginProcessor::stateGeneration() const
{
    const juce::ScopedLock lock(stateLock);
    return generation;
}

void PluginProcessor::updateSession(const juce::String& folder, const juce::String& sample)
{
    const juce::ScopedLock lock(stateLock);
    state.lastFolder = folder;
    state.lastSamplePath = sample;
}

void PluginProcessor::updateWindowSize(int width, int height)
{
    const juce::ScopedLock lock(stateLock);
    state.windowWidth = juce::jlimit(PluginState::minimumWidth, PluginState::maximumWidth, width);
    state.windowHeight = juce::jlimit(PluginState::minimumHeight, PluginState::maximumHeight, height);
}

void PluginProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    setRateAndBufferSizeDetails(sampleRate, maximumExpectedSamplesPerBlock);
    hostEngine.startHostAudio(sampleRate, maximumExpectedSamplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    hostEngine.stopHostAudio();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    // Preview audio and host MIDI go through the same path the standalone app feeds from an
    // audio device: the host supplies the sample rate, block size, clock and keyboard.
    hostEngine.processHostBlock(buffer, midiMessages);
    midiMessages.clear();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const { return true; }

const juce::String PluginProcessor::getName() const { return productName; }

bool PluginProcessor::acceptsMidi() const { return true; }

bool PluginProcessor::producesMidi() const { return false; }

bool PluginProcessor::isMidiEffect() const { return false; }

double PluginProcessor::getTailLengthSeconds() const { return 0.0; }

int PluginProcessor::getNumPrograms() { return 1; }

int PluginProcessor::getCurrentProgram() { return 0; }

void PluginProcessor::setCurrentProgram(int) {}

const juce::String PluginProcessor::getProgramName(int) { return productName; }

void PluginProcessor::changeProgramName(int, const juce::String&) {}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    destData = pluginState().toMemoryBlock();
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    setPluginState(PluginState::fromMemoryBlock(data, sizeInBytes > 0 ? (size_t) sizeInBytes : 0));
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}
} // namespace sonicator::plugin
