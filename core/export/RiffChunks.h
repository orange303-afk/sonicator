#pragma once
#include <juce_core/juce_core.h>

namespace sonicator::riff
{
// Rewrites a RIFF/WAVE file in place: every existing chunk with `id` is removed, then the new one
// is appended. Other chunks and their bytes are copied unchanged and the RIFF size is fixed up.
// Returns false for anything that is not a RIFF/WAVE file with well-formed chunks (RF64 included).
bool insertOrReplaceChunk(const juce::File& wav, const char* id, const juce::MemoryBlock& payload);
bool removeChunk(const juce::File& wav, const char* id);
// Copies the payload of the first chunk with `id`; false when the file or chunk is missing.
bool readChunk(const juce::File& wav, const char* id, juce::MemoryBlock& payload);

// 24-byte ACID payload: flags, root, reserved, beats, 4/4 meter and the tempo float.
// A negative `rootMidi` leaves the root bit clear.
juce::MemoryBlock buildAcid(double bpm, int beats, int rootMidi);

// 36-byte `smpl` payload with a unity note and no loops. When `existing` holds a 36-byte-or-longer
// payload, it is copied and only the unity note is replaced, so loops and sampler data survive.
juce::MemoryBlock buildSmplRoot(int rootMidi, const juce::MemoryBlock& existing = {});
} // namespace sonicator::riff
