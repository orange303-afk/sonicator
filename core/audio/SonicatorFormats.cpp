#include "SonicatorFormats.h"
#include "core/emu/EblAudioFormat.h"
namespace sonicator {
void registerSonicatorFormats(juce::AudioFormatManager& manager)
{
    manager.registerBasicFormats();
    manager.registerFormat(new EblAudioFormat(), false);
}
} // namespace sonicator
