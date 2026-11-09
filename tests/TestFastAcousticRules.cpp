#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/FastAcousticRules.h"

using namespace sonicator;

TEST_CASE("FastAcousticRules evaluates Kick correctly", "[dsp][rules]")
{
    SECTION("Sub-dominant kick")
    {
        AcousticDescriptors desc;
        desc.subBandEnergy = { 0.55, 0.25, 0.10, 0.07, 0.03 };
        desc.logAttackTime = -2.1; // ~8 ms
        desc.crestFactor = 5.2;
        desc.zeroCrossingRate = 450.0;
        desc.decayRate = 45.0;
        desc.pitchClarity = 0.4;
        const double duration = 0.8;

        auto decision = FastAcousticRules::evaluate(desc, duration);
        REQUIRE(decision.determined);
        REQUIRE(decision.canonicalType == "Kick");
        REQUIRE(decision.confidence >= 0.88);
        REQUIRE(decision.confidence <= 0.95);
        REQUIRE(decision.ruleReason == "Sub-dominant transient with rapid attack");
    }

    SECTION("Punchy low-mid kick with sub+low dominant")
    {
        AcousticDescriptors desc;
        desc.subBandEnergy = { 0.32, 0.42, 0.15, 0.08, 0.03 }; // sub=0.32, sub+low=0.74
        desc.logAttackTime = -1.9;
        desc.crestFactor = 4.5;
        desc.zeroCrossingRate = 800.0;
        const double duration = 0.5;

        auto decision = FastAcousticRules::evaluate(desc, duration);
        REQUIRE(decision.determined);
        REQUIRE(decision.canonicalType == "Kick");
        REQUIRE(decision.confidence >= 0.88);
        REQUIRE(decision.confidence <= 0.95);
        REQUIRE(decision.ruleReason == "Sub-dominant transient with rapid attack");
    }
}

TEST_CASE("FastAcousticRules evaluates Snare correctly", "[dsp][rules]")
{
    AcousticDescriptors desc;
    desc.subBandEnergy = { 0.05, 0.25, 0.20, 0.30, 0.20 }; // low+mid = 0.45, high+air = 0.50
    desc.logAttackTime = -2.0;
    desc.crestFactor = 4.0;
    desc.zeroCrossingRate = 2800.0;
    desc.pitchClarity = 0.25;
    const double duration = 0.9;

    auto decision = FastAcousticRules::evaluate(desc, duration);
    REQUIRE(decision.determined);
    REQUIRE(decision.canonicalType == "Snare");
    REQUIRE(decision.confidence >= 0.85);
    REQUIRE(decision.confidence <= 0.92);
    REQUIRE(decision.ruleReason == "Dual-body and high-frequency noise transient");
}

TEST_CASE("FastAcousticRules evaluates Hat correctly", "[dsp][rules]")
{
    SECTION("Short closed hi-hat")
    {
        AcousticDescriptors desc;
        desc.subBandEnergy = { 0.01, 0.04, 0.15, 0.40, 0.40 }; // high+air = 0.80, sub = 0.01
        desc.logAttackTime = -2.4;
        desc.crestFactor = 6.0;
        desc.zeroCrossingRate = 6500.0;
        desc.decayRate = 120.0;
        const double duration = 0.3;

        auto decision = FastAcousticRules::evaluate(desc, duration);
        REQUIRE(decision.determined);
        REQUIRE(decision.canonicalType == "Hat");
        REQUIRE(decision.confidence >= 0.88);
        REQUIRE(decision.confidence <= 0.95);
        REQUIRE(decision.ruleReason == "High-frequency short transient");
    }

    SECTION("Longer pedal hat with fast decay")
    {
        AcousticDescriptors desc;
        desc.subBandEnergy = { 0.02, 0.05, 0.20, 0.35, 0.38 }; // high+air = 0.73
        desc.logAttackTime = -1.9;
        desc.crestFactor = 4.5;
        desc.zeroCrossingRate = 4200.0;
        desc.decayRate = 95.0; // >= 80
        const double duration = 1.0; // <= 1.2

        auto decision = FastAcousticRules::evaluate(desc, duration);
        REQUIRE(decision.determined);
        REQUIRE(decision.canonicalType == "Hat");
        REQUIRE(decision.confidence >= 0.88);
        REQUIRE(decision.confidence <= 0.95);
        REQUIRE(decision.ruleReason == "High-frequency short transient");
    }
}

TEST_CASE("FastAcousticRules evaluates Cymbal correctly", "[dsp][rules]")
{
    SECTION("Sustained crash cymbal")
    {
        AcousticDescriptors desc;
        desc.subBandEnergy = { 0.01, 0.04, 0.20, 0.35, 0.40 }; // high+air = 0.75, sub = 0.01
        desc.logAttackTime = -2.0;
        desc.crestFactor = 3.5;
        desc.zeroCrossingRate = 5000.0;
        desc.decayRate = 15.0;
        desc.pitchClarity = 0.20;
        const double duration = 2.2; // > 1.2s

        auto decision = FastAcousticRules::evaluate(desc, duration);
        REQUIRE(decision.determined);
        REQUIRE(decision.canonicalType == "Cymbal");
        REQUIRE(decision.confidence >= 0.85);
        REQUIRE(decision.confidence <= 0.92);
        REQUIRE(decision.ruleReason == "High-frequency sustained texture");
    }

    SECTION("Ride cymbal with moderate duration and slow decay")
    {
        AcousticDescriptors desc;
        desc.subBandEnergy = { 0.02, 0.08, 0.30, 0.30, 0.30 }; // high+air = 0.60
        desc.logAttackTime = -1.9;
        desc.decayRate = 35.0; // < 50
        desc.pitchClarity = 0.30;
        const double duration = 1.0; // > 0.8s

        auto decision = FastAcousticRules::evaluate(desc, duration);
        REQUIRE(decision.determined);
        REQUIRE(decision.canonicalType == "Cymbal");
        REQUIRE(decision.confidence >= 0.85);
        REQUIRE(decision.confidence <= 0.92);
        REQUIRE(decision.ruleReason == "High-frequency sustained texture");
    }
}

TEST_CASE("FastAcousticRules evaluates Sub bass correctly", "[dsp][rules]")
{
    AcousticDescriptors desc;
    desc.subBandEnergy = { 0.85, 0.10, 0.03, 0.01, 0.01 }; // sub = 0.85, high+air = 0.02
    desc.logAttackTime = -1.2; // slower attack
    desc.crestFactor = 1.8;
    desc.zeroCrossingRate = 110.0;
    desc.pitchClarity = 0.92;
    desc.decayRate = 15.0;
    const double duration = 1.8;

    auto decision = FastAcousticRules::evaluate(desc, duration);
    REQUIRE(decision.determined);
    REQUIRE(decision.canonicalType == "Sub");
    REQUIRE(decision.confidence == Catch::Approx(0.90));
    REQUIRE(decision.ruleReason == "Tonal pure low-frequency sustain");
}

TEST_CASE("FastAcousticRules leaves ambiguous/melodic samples undetermined", "[dsp][rules]")
{
    SECTION("Synth pad")
    {
        AcousticDescriptors desc;
        desc.subBandEnergy = { 0.10, 0.30, 0.40, 0.15, 0.05 };
        desc.logAttackTime = -0.5; // slow attack
        desc.crestFactor = 2.0;
        desc.zeroCrossingRate = 1200.0;
        desc.pitchClarity = 0.70;
        const double duration = 3.0;

        auto decision = FastAcousticRules::evaluate(desc, duration);
        REQUIRE_FALSE(decision.determined);
        REQUIRE(decision.canonicalType.isEmpty());
        REQUIRE(decision.confidence == 0.0);
    }

    SECTION("Silence / empty descriptors")
    {
        AcousticDescriptors desc {};
        auto decision = FastAcousticRules::evaluate(desc, 0.0);
        REQUIRE_FALSE(decision.determined);
        REQUIRE(decision.canonicalType.isEmpty());
        REQUIRE(decision.confidence == 0.0);
    }
}
