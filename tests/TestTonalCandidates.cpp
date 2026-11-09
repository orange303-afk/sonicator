#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/TonalCandidates.h"
#include <cmath>
using namespace sonicator;
namespace {
constexpr double rate = 44100.0;

juce::AudioBuffer<float> makeBuffer(double seconds, double sampleRate = rate)
{
    juce::AudioBuffer<float> buffer(1, (int) std::llround(seconds * sampleRate));
    buffer.clear();
    return buffer;
}

void addSine(juce::AudioBuffer<float>& buffer, double sampleRate, double frequency,
             double amplitude, double startSeconds = 0.0, double durationSeconds = 2.0)
{
    const int start = (int) std::llround(startSeconds * sampleRate);
    const int length = (int) std::llround(durationSeconds * sampleRate);
    const int fade = (int) std::llround(0.01 * sampleRate);
    for (int i = 0; i < length && start + i < buffer.getNumSamples(); ++i)
    {
        double envelope = 1.0;
        if (i < fade) envelope = (double) i / fade;
        if (i > length - fade) envelope = (double) (length - i) / fade;
        buffer.addSample(0, start + i, (float) (amplitude * envelope
            * std::sin(2.0 * juce::MathConstants<double>::pi * frequency * (double) i / sampleRate)));
    }
}

double midiFrequency(double midi)
{
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

juce::AudioBuffer<float> triad(int rootPitchClass, int third, double detuneCents = 0.0)
{
    auto buffer = makeBuffer(2.0);
    const double root = 60.0 + rootPitchClass;
    for (double note : { root, root + third, root + 7.0 })
        addSine(buffer, rate, midiFrequency(note + detuneCents / 100.0), 0.16);
    return buffer;
}

juce::AudioBuffer<float> singleNote(double midi, double detuneCents = 0.0)
{
    auto buffer = makeBuffer(2.0);
    addSine(buffer, rate, midiFrequency(midi + detuneCents / 100.0), 0.5);
    return buffer;
}

AnalysisAudio audioOf(const juce::AudioBuffer<float>& buffer)
{
    AnalysisAudio audio;
    audio.buffer = buffer;
    audio.sampleRate = rate;
    return audio;
}

SmartCandidate candidate(SmartField field, juce::String source, juce::String text, double number,
                         CandidateStatus status = CandidateStatus::Valid)
{
    SmartCandidate c;
    c.field = field;
    c.source = std::move(source);
    c.textValue = std::move(text);
    c.numericValue = number;
    c.status = status;
    return c;
}

const SmartCandidate* findField(const std::vector<SmartCandidate>& list, SmartField field)
{
    for (const auto& c : list) if (c.field == field) return &c;
    return nullptr;
}
} // namespace

TEST_CASE("A metadata key suppresses the DSP key", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(triad(9, 3)),
        { candidate(SmartField::Key, "filename", "C major", 0) }, "loop", "keys");
    REQUIRE(findField(extraction.candidates, SmartField::Key) == nullptr);
}

TEST_CASE("A valid pitch suppresses the DSP pitch", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(singleNote(69)),
        { candidate(SmartField::Pitch, "wav_smpl", {}, 60.0) }, "one-shot", "bass");
    REQUIRE(findField(extraction.candidates, SmartField::Pitch) == nullptr);
}

TEST_CASE("A valid root suppresses the DSP pitch", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(singleNote(69)),
        { candidate(SmartField::RootNote, "wav_smpl", {}, 57.0) }, "one-shot", "bass");
    REQUIRE(findField(extraction.candidates, SmartField::Pitch) == nullptr);
}

TEST_CASE("A default sampler root does not suppress the DSP pitch", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(singleNote(69)),
        { candidate(SmartField::RootNote, "wav_smpl", {}, 60.0, CandidateStatus::DefaultValue) },
        "one-shot", "bass");
    REQUIRE(findField(extraction.candidates, SmartField::Pitch) != nullptr);
}

TEST_CASE("A loop never gets a DSP pitch", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(singleNote(69)), {}, "loop", "bass");
    REQUIRE(findField(extraction.candidates, SmartField::Pitch) == nullptr);
}

TEST_CASE("A one-shot with no pitch metadata gets a DSP pitch", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(singleNote(69)), {}, "one-shot", "bass");
    const auto* pitch = findField(extraction.candidates, SmartField::Pitch);
    REQUIRE(pitch != nullptr);
    REQUIRE(pitch->source == "dsp");
    REQUIRE(pitch->status == CandidateStatus::Valid);
    REQUIRE(pitch->numericValue == Catch::Approx(69.0).margin(0.02));
    REQUIRE(pitch->auxValue == 0.0);
    REQUIRE(pitch->limitation.isNotEmpty());
}

TEST_CASE("Drum types skip key and pitch", "[tonal][candidates]")
{
    for (const auto& type : { "kick", "snare", "hat", "percussion", "cymbal", "clap" })
    {
        INFO("type " << type);
        const auto extraction = extractTonalCandidates(audioOf(triad(0, 4)),
            {}, "one-shot", type);
        REQUIRE(findField(extraction.candidates, SmartField::Key) == nullptr);
        REQUIRE(findField(extraction.candidates, SmartField::Pitch) == nullptr);
    }
}

TEST_CASE("Vocal and fx types skip pitch but not key", "[tonal][candidates]")
{
    for (const auto& type : { "vocal", "fx" })
    {
        INFO("type " << type);
        const auto extraction = extractTonalCandidates(audioOf(triad(0, 4)),
            {}, "one-shot", type);
        REQUIRE(findField(extraction.candidates, SmartField::Pitch) == nullptr);
        REQUIRE(findField(extraction.candidates, SmartField::Key) != nullptr);
    }
}

TEST_CASE("Melodic and empty types allow pitch", "[tonal][candidates]")
{
    for (const auto& type : { "bass", "keys", "guitar", "lead", "" })
    {
        INFO("type " << type);
        const auto extraction = extractTonalCandidates(audioOf(singleNote(69)),
            {}, "one-shot", type);
        REQUIRE(findField(extraction.candidates, SmartField::Pitch) != nullptr);
    }
}

TEST_CASE("A confident tonic without a mode is a bare key name", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(singleNote(60)), {}, "loop", "keys");
    const auto* key = findField(extraction.candidates, SmartField::Key);
    REQUIRE(key != nullptr);
    REQUIRE(key->textValue == "C");
    REQUIRE(key->auxValue >= 0.0);
    REQUIRE(key->auxValue <= 1.0);
    REQUIRE(key->limitation.isNotEmpty());
}

TEST_CASE("A full key carries its mode", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(triad(1, 3)), {}, "loop", "keys");
    const auto* key = findField(extraction.candidates, SmartField::Key);
    REQUIRE(key != nullptr);
    REQUIRE(key->textValue == "C# minor");
}

TEST_CASE("An ambiguous tonic produces no key candidate", "[tonal][candidates]")
{
    auto buffer = makeBuffer(2.0);
    for (int root : { 0, 6 })
    {
        const double midiRoot = 60.0 + root;
        for (double note : { midiRoot, midiRoot + 4.0, midiRoot + 7.0 })
            addSine(buffer, rate, midiFrequency(note), 0.10);
    }
    const auto extraction = extractTonalCandidates(audioOf(buffer), {}, "loop", "keys");
    REQUIRE(findField(extraction.candidates, SmartField::Key) == nullptr);
}

TEST_CASE("Pitch stores the fractional MIDI and its cents", "[tonal][candidates]")
{
    const auto extraction = extractTonalCandidates(audioOf(singleNote(69, 25.0)),
        {}, "one-shot", "bass");
    const auto* pitch = findField(extraction.candidates, SmartField::Pitch);
    REQUIRE(pitch != nullptr);
    REQUIRE(pitch->numericValue == Catch::Approx(69.25).margin(0.03));
    REQUIRE((pitch->numericValue - std::round(pitch->numericValue)) * 100.0
            == Catch::Approx(25.0).margin(3.0));
}
