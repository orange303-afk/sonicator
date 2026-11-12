#pragma once
#include "core/sampler/SamplerTypes.h"
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
namespace sonicator::krz {
// Kurzweil K2000/K2500/K2600 big-endian object bank (`.krz`, `.k25`, `.k26`). The layout and every
// corpus-derived decision are documented in docs/KURZWEIL_KRZ.md. Reading is bounded: every offset
// and span is checked against the file before it is used.
struct SoundFileHeader
{
    int rootKey = 60;
    bool looping = false;
    bool ramBased = true;      // false: the span points into the K2000 factory ROM, not the file
    int playbackMode = 0;      // 0 normal, 1 reverse, 2 bidirectional: only 0 is played
    int volumeHalfDb = 0;
    int64_t startWord = 0, loopWord = 0, endWord = 0;  // word offsets into the bank's PCM region
    int sampleRate = 0;
};
struct SampleObject
{
    int id = 0;
    juce::String name;
    bool stereo = false;
    std::vector<SoundFileHeader> headers;
};
struct KeymapEntry
{
    int sampleId = 0;
    int subSample = 1;
    int tuningCents = 0;
    int volumeHalfDb = 0;
};
// levels[level][midiNote] with level 0 = ppp and level 7 = fff. Notes below C0 (MIDI 12) stay empty.
struct Keymap
{
    int id = 0;
    juce::String name;
    int defaultSampleId = 0;
    std::array<std::vector<KeymapEntry>, 8> levels;
};
struct Layer
{
    int keymap1 = 0, keymap2 = 0;
};
struct Program
{
    int id = 0;
    juce::String name;
    std::vector<Layer> layers;
};
struct Bank
{
    juce::String magic;   // "PRAM" or "SROM"
    std::vector<SampleObject> samples;
    std::vector<Keymap> keymaps;
    std::vector<Program> programs;
    // Signed 16-bit words of every available part, in order, native-endian.
    std::vector<juce::int16> pcm;
    juce::StringArray warnings;
    juce::String missingPart;   // file name of the next part that is not present, empty when complete
    bool hasPcm() const { return ! pcm.empty(); }
};
// One decoded sample header. Loop bounds are frames inside the returned audio, end exclusive.
struct DecodedSample
{
    SampleAudioPtr audio;
    int rootKey = 60;
    bool looping = false;
    int64_t loopStart = 0, loopEnd = 0;
};

bool isKrzFile(const juce::File&);
// 0 when the name is not `<stem>.krN.<krz|k25|k26>`, otherwise N.
int continuationPartNumber(const juce::File&);
// The `.kr1` file of a set, or the file itself when it is not a continuation part.
juce::File firstPartOf(const juce::File&);
// Reads all objects from the first part and the PCM of every part present. True for a bank with no
// sample data. False with a reason for a missing file, a foreign signature or a broken layout.
bool readBank(const juce::File&, Bank&, juce::String& error);
// One header of a sample object as mono audio; a stereo object's second header is its right side.
bool decodeHeader(const Bank&, const SampleObject&, int headerIndex, DecodedSample&, juce::String&);
// A stereo sample object as one two-channel source. False for a mono object.
bool decodeStereo(const Bank&, const SampleObject&, DecodedSample&, juce::String&);
} // namespace sonicator::krz
