#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace sonicator {

class MelSpectrogram {
public:
    // Computes log-mel spectrogram (melBands x numFrames) flattened in row-major order: [band0_frame0, band0_frame1, ...]
    // numFrames = (numSamples - fftSize) / hopSize + 1
    static std::vector<float> computeLogMel(const juce::AudioBuffer<float>& buffer,
                                            double sampleRate,
                                            int melBands = 64,
                                            int fftSize = 1024,
                                            int hopSize = 512);

    static int calculateNumFrames(int numSamples, int fftSize = 1024, int hopSize = 512);
};

} // namespace sonicator
