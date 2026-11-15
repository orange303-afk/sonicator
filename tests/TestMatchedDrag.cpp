#include <catch2/catch_test_macros.hpp>
#include "core/match/MatchedDrag.h"
#include "TestHelpers.h"

using namespace sonicator;

TEST_CASE ("Match off leaves the ordinary drag path alone", "[plugin-match][drag]")
{
    TestDirectory t;
    const auto decision = resolveMatchedDrag (false, 1, true, true,
                                             t.dir.getChildFile ("ready.wav"));
    CHECK (decision.status == MatchedDragDecision::Status::bypass);
    CHECK_FALSE (decision.handled());
    CHECK_FALSE (decision.startsDrag());
    CHECK (decision.file == juce::File());
}

TEST_CASE ("A ready matched result is the file every drag entry point gets",
           "[plugin-match][drag]")
{
    TestDirectory t;
    const auto prepared = t.dir.getChildFile ("prepared.wav");
    prepared.replaceWithText ("audio");
    const auto decision = resolveMatchedDrag (true, 1, true, true, prepared);
    REQUIRE (decision.status == MatchedDragDecision::Status::ready);
    CHECK (decision.startsDrag());
    CHECK (decision.file == prepared);
    CHECK (decision.message.isEmpty());
}

TEST_CASE ("Pending and blocked Match never start a drag", "[plugin-match][drag]")
{
    TestDirectory t;
    auto pending = resolveMatchedDrag (true, 1, true, false, {});
    CHECK (pending.status == MatchedDragDecision::Status::pending);
    CHECK_FALSE (pending.startsDrag());
    CHECK (pending.message.contains ("Preparing"));

    auto batch = resolveMatchedDrag (true, 2, true, true, t.dir.getChildFile ("x.wav"));
    CHECK (batch.status == MatchedDragDecision::Status::blocked);
    CHECK (batch.message == "Select one sample for Match");
    CHECK (batch.file == juce::File());

    auto other = resolveMatchedDrag (true, 1, false, true, t.dir.getChildFile ("x.wav"));
    CHECK (other.status == MatchedDragDecision::Status::blocked);
    CHECK_FALSE (other.startsDrag());

    auto blocked = resolveMatchedDrag (true, 1, true, false, {}, "Host tempo unavailable");
    CHECK (blocked.status == MatchedDragDecision::Status::blocked);
    CHECK (blocked.message == "Host tempo unavailable");

    // A ready flag with a missing file is not a drag either.
    auto missing = resolveMatchedDrag (true, 1, true, true, {});
    CHECK_FALSE (missing.startsDrag());
}
