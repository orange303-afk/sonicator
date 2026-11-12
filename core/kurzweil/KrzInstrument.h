#pragma once
#include "core/sampler/FileInstrument.h"
namespace sonicator {
// Turns a Kurzweil bank into the shared external-instrument model: one program per K2000 program,
// zones from its layers' keymaps, and one decoded source per RAM sample header (a stereo sample
// object becomes one two-channel source). Banks that only reference the factory ROM are refused.
bool loadKrzInstrument(const juce::File&, FileInstrument&, juce::String& error);
// Copies the zones of one program into the instrument's active zone list.
bool selectInstrumentProgram(FileInstrument&, int index, juce::String& error);
} // namespace sonicator
