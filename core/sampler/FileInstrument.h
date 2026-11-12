#pragma once
#include "SamplerTypes.h"
#include <juce_core/juce_core.h>
#include <vector>
namespace sonicator {
enum class FileInstrumentFormat { exs24, nki, krz };
// One zone of an external-file instrument. `sampleIndex` addresses FileInstrument::sources and
// ::audio; start/end and loop bounds are frame positions inside that decoded source.
struct ExternalZone
{
    int keyLow = 0, keyHigh = 127, velocityLow = 0, velocityHigh = 127;
    int rootKey = 60, sampleIndex = -1;
    double tuningCents = 0, attenuationDb = 0, pan = 0;
    int64_t start = 0, end = 0, loopStart = 0, loopEnd = 0;
    bool looping = false;
    bool oneShot = false;
};
// One program of a multi-program bank (a Kurzweil K2000 program). EXS24 and NKI leave this empty.
struct InstrumentProgram
{
    juce::String name;
    int number = 0;
    std::vector<ExternalZone> zones;
};
struct FileInstrument
{
    juce::File file;
    juce::String name;
    std::vector<SampleDescriptor> sources;
    std::vector<SampleAudioPtr> audio;
    std::vector<ExternalZone> zones;
    std::vector<InstrumentProgram> programs;
    int currentProgram = -1;
    juce::StringArray warnings;
    bool partial = false;
    bool isPlayable() const { return ! zones.empty(); }
};
// Reads EXS24 or supported NKI XML instruments read-only, resolves external samples and decodes
// them into immutable snapshots. Missing or ambiguous samples are explicit; originals never move.
bool loadFileInstrument(const juce::File&, FileInstrumentFormat, FileInstrument&, juce::String& error);
// Selects a program of a multi-program bank and copies its zones into `zones`.
bool selectInstrumentProgram(FileInstrument&, int index, juce::String& error);
bool isExs24File(const juce::File&);
// detail explains the recognised container and whether the built-in reader supports it.
bool isNkiFile(const juce::File&, juce::String& detail);
} // namespace sonicator
