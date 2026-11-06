#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
struct TestDirectory
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-test", "");
    TestDirectory() { dir.createDirectory(); }
    ~TestDirectory() { dir.deleteRecursively(); }
    juce::File wave(juce::String name = "sine.wav", double hz = 440.0, int channels = 1,
                    int samples = 44100, bool invertedStereo = false)
    {
        auto f = dir.getChildFile(name);
        f.getParentDirectory().createDirectory();
        f.deleteFile(); // FileOutputStream appends to existing files; fixtures are replaced, never appended.
        auto out = f.createOutputStream();
        const int bytes = samples * channels * 2;
        out->write("RIFF", 4); out->writeInt(bytes + 36); out->write("WAVEfmt ", 8);
        out->writeInt(16); out->writeShort(1); out->writeShort((short) channels);
        out->writeInt(44100); out->writeInt(44100 * channels * 2);
        out->writeShort((short) (channels * 2)); out->writeShort(16);
        out->write("data", 4); out->writeInt(bytes);
        for (int i = 0; i < samples; ++i)
            for (int c = 0; c < channels; ++c)
            {
                double value = hz == 0 ? 0 : 0.8 * std::sin(2 * juce::MathConstants<double>::pi * hz * i / 44100);
                if (invertedStereo && c == 1) value = -value;
                out->writeShort((short) std::lround(value * 32767));
            }
        return f;
    }
};
