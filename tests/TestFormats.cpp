#include <catch2/catch_test_macros.hpp>
#include "core/dsp/BasicAnalyzer.h"
#include "core/audio/AudioEngine.h"
using namespace sonicator;
TEST_CASE("WAV AIFF FLAC and MP3 decode in analysis and playback", "[formats]")
{
    auto dir = juce::File(SONICATOR_FIXTURE_DIR);
    for (auto ext : {"wav","aiff","flac","mp3"})
    {
        INFO(ext);
        auto file = dir.getChildFile(juce::String("tone.")+ext);
        REQUIRE(file.existsAsFile());
        auto r = BasicAnalyzer().analyze(file);
        REQUIRE(r.error.isEmpty()); REQUIRE(r.duration >= 0.20); REQUIRE(r.duration <= 0.40);
        REQUIRE(r.sampleRate == 48000); REQUIRE(r.peakLevel > 0.01);
        AudioEngine engine(false); REQUIRE(engine.loadFile(file));
        engine.transport().prepareToPlay(256,44100); engine.play();
        juce::AudioBuffer<float> buffer(2,256);
        float renderedPeak = 0;
        // MP3 has decoder/encoder priming; inspect 0.18 s, not just the silent first 12 ms.
        for (int block = 0; block < 32; ++block)
        {
            engine.transport().getNextAudioBlock({&buffer,0,256});
            renderedPeak = juce::jmax(renderedPeak,buffer.getMagnitude(0,256));
        }
        REQUIRE(renderedPeak > 0.01f);
    }
}
