#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>
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
    // Minimal AIFC with a COMM compression type. Used to test codecs without decoding audio.
    juce::File aifc(const juce::String& name, const char* codec)
    {
        auto be32 = [](std::vector<unsigned char>& b, juce::uint32 x)
        { for (int i = 3; i >= 0; --i) b.push_back((unsigned char) ((x >> (8 * i)) & 0xff)); };
        auto ascii = [](std::vector<unsigned char>& b, const char* text)
        { b.insert(b.end(), text, text + 4); };
        std::vector<unsigned char> comm { 0, 2 };
        be32(comm, 1000);
        comm.push_back(0); comm.push_back(16);
        const unsigned char rate[10] { 0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0 };
        comm.insert(comm.end(), rate, rate + 10);
        ascii(comm, codec);
        std::vector<unsigned char> body;
        ascii(body, "FVER"); be32(body, 4); be32(body, 0xa2805140);
        ascii(body, "COMM"); be32(body, (juce::uint32) comm.size());
        body.insert(body.end(), comm.begin(), comm.end());
        std::vector<unsigned char> file;
        ascii(file, "FORM"); be32(file, (juce::uint32) body.size() + 4); ascii(file, "AIFC");
        file.insert(file.end(), body.begin(), body.end());
        auto f = dir.getChildFile(name);
        f.replaceWithData(file.data(), file.size());
        return f;
    }
};
