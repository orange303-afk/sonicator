#include "SonicatorFormats.h"
#include "core/emu/EblAudioFormat.h"
#include "core/voc/VocAudioFormat.h"
namespace sonicator {
void registerSonicatorFormats(juce::AudioFormatManager& manager)
{
    manager.registerBasicFormats();
    manager.registerFormat(new EblAudioFormat(), false);
    manager.registerFormat(new VocAudioFormat(), false);
}
} // namespace sonicator
