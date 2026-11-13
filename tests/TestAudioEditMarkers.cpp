#include <catch2/catch_test_macros.hpp>
#include "core/edit/AudioEditMarkers.h"

using namespace sonicator;
using namespace sonicator::edit;

namespace
{
FileMarkers markers(int64_t loopStart, int64_t loopEnd, std::vector<int64_t> cues)
{
    FileMarkers m;
    m.hasLoop = true;
    m.loopStart = loopStart;
    m.loopEndExclusive = loopEnd;
    m.cuePositions = std::move(cues);
    return m;
}
} // namespace

TEST_CASE("Delete drops points inside the removed range and shifts the tail", "[edit][markers]")
{
    const auto out = transformMarkers(markers(100, 400, { 50, 150, 350 }),
                                      EditOperation::deleteRegion, { 100, 200 }, 1000);
    REQUIRE(out.hasLoop);
    REQUIRE(out.loopStart == 100);
    REQUIRE(out.loopEndExclusive == 300);
    REQUIRE(out.cuePositions == std::vector<int64_t> { 50, 250 });
}

TEST_CASE("Crop rebases markers onto the kept range", "[edit][markers]")
{
    const auto out = transformMarkers(markers(100, 400, { 50, 350, 399 }),
                                      EditOperation::cropToSelection, { 100, 400 }, 1000);
    REQUIRE(out.hasLoop);
    REQUIRE(out.loopStart == 0);
    REQUIRE(out.loopEndExclusive == 300);
    REQUIRE(out.cuePositions == std::vector<int64_t> { 250, 299 });
}

TEST_CASE("Reverse mirrors loop and cue positions inside the range", "[edit][markers]")
{
    const auto out = transformMarkers(markers(100, 300, { 200 }),
                                      EditOperation::reverse, { 0, 1000 }, 1000);
    REQUIRE(out.hasLoop);
    REQUIRE(out.loopStart == 700);
    REQUIRE(out.loopEndExclusive == 900);
    REQUIRE(out.cuePositions == std::vector<int64_t> { 799 });
}

TEST_CASE("A loop that becomes invalid is removed", "[edit][markers]")
{
    const auto out = transformMarkers(markers(0, 100, {}),
                                      EditOperation::deleteRegion, { 0, 100 }, 1000);
    REQUIRE_FALSE(out.hasLoop);
}

TEST_CASE("Trim silence shifts markers by the trimmed head", "[edit][markers]")
{
    const auto out = transformMarkers(markers(1000, 2000, { 900, 1500, 2100 }),
                                      EditOperation::trimSilence, { 200, 2200 }, 3000);
    REQUIRE(out.hasLoop);
    REQUIRE(out.loopStart == 800);
    REQUIRE(out.loopEndExclusive == 1800);
    REQUIRE(out.cuePositions == std::vector<int64_t> { 700, 1300, 1900 });
}

TEST_CASE("Gain and normalize leave markers untouched", "[edit][markers]")
{
    const auto original = markers(1000, 2000, { 1500 });
    for (auto op : { EditOperation::normalize, EditOperation::gain, EditOperation::fadeIn,
                     EditOperation::silenceRegion, EditOperation::toMono,
                     EditOperation::removeDcOffset })
        REQUIRE(transformMarkers(original, op, { 0, 100 }, 3000) == original);
}

TEST_CASE("Buffer edits move its markers", "[edit][markers]")
{
    SampleAudio audio;
    audio.sampleRate = 44100;
    audio.channels = 1;
    audio.frames = 1000;
    audio.left.assign(1000, 0.5f);
    auto buffer = AudioEditBuffer::fromSampleAudio(audio, markers(100, 400, { 150 }));
    REQUIRE(buffer.apply({ EditOperation::deleteRegion, { 100, 200 } }));
    REQUIRE(buffer.markers().hasLoop);
    REQUIRE(buffer.markers().loopStart == 100);
    REQUIRE(buffer.markers().loopEndExclusive == 300);
    REQUIRE(buffer.markers().cuePositions.empty());
    REQUIRE(buffer.undo());
    REQUIRE(buffer.markers().loopEndExclusive == 400);
    REQUIRE(buffer.markers().cuePositions == std::vector<int64_t> { 150 });
}
