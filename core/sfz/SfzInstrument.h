#pragma once
#include "core/sampler/FileInstrument.h"
#include <juce_core/juce_core.h>
namespace sonicator {
// Reads a text SFZ instrument, resolves its external samples relative to the file and turns each
// <region> into a zone of the shared FileInstrument model. Inheritance follows master -> group ->
// region. Only the playback-relevant opcodes are interpreted; effects, filters and envelopes are
// ignored, like the VAST part of a Kurzweil program.
bool loadSfzInstrument(const juce::File&, FileInstrument&, juce::String& error);
// True when the file is a readable text instrument with an SFZ section marker. `detail` explains a
// refusal for the catalogue.
bool isSfzFile(const juce::File&, juce::String* detail = nullptr);
} // namespace sonicator
