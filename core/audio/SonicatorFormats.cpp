#include "SonicatorFormats.h"
#include "core/caf/CafAudioFormat.h"
#include "core/emu/EblAudioFormat.h"
#include "core/voc/VocAudioFormat.h"
namespace sonicator {
void registerSonicatorFormats(juce::AudioFormatManager& manager)
{
    // Sonicator's own readers go in before JUCE's: on macOS registerBasicFormats() also claims
    // `.caf` through CoreAudioFormat, and the catalog must decode the same way on every platform.
    manager.registerFormat(new CafAudioFormat(), false);
    manager.registerBasicFormats();
    manager.registerFormat(new EblAudioFormat(), false);
    manager.registerFormat(new VocAudioFormat(), false);
}
} // namespace sonicator
