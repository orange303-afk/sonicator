#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/preview/SpectrogramService.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
bool settled(const SpectrogramService& service)
{
    const auto phase = service.state().phase;
    return phase == SpectrogramService::Phase::ready || phase == SpectrogramService::Phase::failed;
}

SpectrogramRequest fileRequest(const juce::File& file, double start, double end,
                               const juce::String& tag)
{
    SpectrogramRequest request;
    request.file = file;
    request.key = SpectrogramService::sourceKey(file) + "|" + tag;
    request.startSeconds = start;
    request.endSeconds = end;
    request.params.rows = 48;
    request.params.columns = 96;
    return request;
}
} // namespace

TEST_CASE("The service reads a file and publishes a grid", "[spectrogram][service]")
{
    TestDirectory t;
    const auto file = t.wave("sine.wav", 440.0, 1, 22050);
    SpectrogramService service;
    const auto request = fileRequest(file, 0.0, 0.5, "whole");
    service.prepare(request);
    REQUIRE(pumpUntil([&] { return settled(service); }));
    const auto state = service.state();
    REQUIRE(state.phase == SpectrogramService::Phase::ready);
    REQUIRE(state.grid != nullptr);
    REQUIRE(state.grid->valid());
    REQUIRE(state.key == request.key);
    REQUIRE(state.grid->endSeconds == Approx(0.5).margin(0.01));
}

TEST_CASE("A newer request wins over the one in flight", "[spectrogram][service]")
{
    TestDirectory t;
    const auto file = t.wave("sine.wav", 440.0, 1, 22050);
    SpectrogramService service;
    service.prepare(fileRequest(file, 0.0, 0.2, "first"));
    const auto second = fileRequest(file, 0.2, 0.8, "second");
    service.prepare(second);
    REQUIRE(pumpUntil([&] { return settled(service); }));
    REQUIRE(service.state().phase == SpectrogramService::Phase::ready);
    REQUIRE(service.state().key == second.key);
    REQUIRE(service.state().grid->startSeconds == Approx(0.2).margin(0.01));
}

TEST_CASE("Cancel drops the request and the result", "[spectrogram][service]")
{
    TestDirectory t;
    const auto file = t.wave("sine.wav", 440.0, 1, 22050);
    SpectrogramService service;
    service.prepare(fileRequest(file, 0.0, 1.0, "cancelled"));
    service.cancel();
    REQUIRE(service.state().phase == SpectrogramService::Phase::idle);
    REQUIRE(service.state().grid == nullptr);
    juce::Thread::sleep(80); // give a worker that ignored the cancel a chance to publish
    REQUIRE(service.state().phase == SpectrogramService::Phase::idle);
}

TEST_CASE("A missing file reports why there is no spectrum", "[spectrogram][service]")
{
    TestDirectory t;
    SpectrogramService service;
    auto request = fileRequest(t.dir.getChildFile("missing.wav"), 0.0, 1.0, "missing");
    service.prepare(request);
    REQUIRE(pumpUntil([&] { return settled(service); }));
    REQUIRE(service.state().phase == SpectrogramService::Phase::failed);
    REQUIRE(service.state().error.isNotEmpty());
    REQUIRE(service.state().grid == nullptr);
}

TEST_CASE("A decoded snapshot needs no file at all", "[spectrogram][service]")
{
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = 44100;
    audio->channels = 2;
    audio->frames = 22050;
    audio->left.resize((size_t) audio->frames);
    audio->right.resize((size_t) audio->frames);
    for (int64_t i = 0; i < audio->frames; ++i)
    {
        const auto value = (float) (0.7 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0
                                                   * (double) i / 44100.0));
        audio->left[(size_t) i] = value;
        audio->right[(size_t) i] = -value;
    }
    SpectrogramService service;
    SpectrogramRequest request;
    request.audio = audio;
    request.key = "snapshot:1";
    request.startSeconds = 0.0;
    request.endSeconds = 0.5;
    request.params.rows = 40;
    request.params.columns = 80;
    service.prepare(request);
    REQUIRE(pumpUntil([&] { return settled(service); }));
    REQUIRE(service.state().phase == SpectrogramService::Phase::ready);
    REQUIRE(service.state().grid->valid());
    float peak = -1000.0f;
    for (float value : service.state().grid->db) peak = juce::jmax(peak, value);
    REQUIRE(peak == Approx(0.0).margin(1e-4));
}
