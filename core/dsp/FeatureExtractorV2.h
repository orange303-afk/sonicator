#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace sonicator {

// A richer description of one sound than the schema-1 vector: spectral shape, band contrast, MFCC,
// per-band envelopes, harmonic/percussive balance, modulation and stereo. Every value is in 0..1 and
// every value is defined for any sample rate, because the analysis is limited to 40..8000 Hz.
struct FeatureVectorV2
{
    std::vector<float> values;
    juce::String error;

    bool ok() const { return error.isEmpty() && ! values.empty(); }
};

// Fixed layout, in this order (84 values):
//   0..13   the schema-1 block: attack, decay, crest, temporal centroid,
//           shares of <80, 80-350, 350-2500, 2500-7000 and 7000-8000 Hz, zero-crossing rate,
//           spectral flux, pitch clarity, duration/30, RMS*2. The top band ends at 8 kHz so the
//           value means the same thing for a 16 kHz source and a 44.1 kHz one.
//   14..31  spectral shape, mean and standard deviation over frames: centroid, spread, skewness,
//           kurtosis, rolloff 85%, rolloff 95%, flatness, entropy, slope (a band tilt in dB)
//   32..37  band contrast for 6 octave bands centred at 100, 300, 800, 2000, 4000 and 7000 Hz
//   38..63  MFCC 1..13, mean and standard deviation over frames
//   64..75  envelopes of <300, 300-1500, 1500-4000 and 4000-8000 Hz: attack, decay, early share
//   76..77  harmonic and percussive ratio
//   78..81  onset density, envelope periodicity, mean onset strength, low-band periodicity
//   82..83  stereo width and channel correlation
//
// Known limits, measured between a 16 kHz and a 48 kHz source of the same sweep: the vector agrees on
// average within 0.06, worst per dimension about 0.5 for a band contrast measured on a moving
// narrowband peak. Skewness and kurtosis are third and fourth moments of a spectral peak and follow
// the analysis bin width, so they are the least transferable dimensions; the training stage's
// ablation is what decides whether they stay.
class FeatureExtractorV2
{
public:
    static constexpr int dimensions() { return 84; }
    static constexpr int version() { return 2; }

    static FeatureVectorV2 extract(const juce::AudioBuffer<float>& buffer, double sampleRate);
    static juce::StringArray dimensionNames();
};

} // namespace sonicator
