#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCSlipperyRisk.h"
#include <limits>

namespace MCSlipperyRiskTests
{
struct FDecisionAt
{
    EMCSlipperyDecision Decision=EMCSlipperyDecision::None;
    float Seconds=0;
};

FDecisionAt Next(FMCSlipperyRisk& Risk,int32 FPS,float MaximumSeconds,bool Carrying=false,
    float FirstSeconds=1.5f,float RepeatSeconds=3.f)
{
    const float Dt=1.f/FPS;
    for(int32 Frame=1;Frame<=FMath::CeilToInt(MaximumSeconds*FPS);++Frame) {
        const auto Decision=Risk.Update(Dt,true,true,true,Carrying,FirstSeconds,RepeatSeconds);
        if(Decision!=EMCSlipperyDecision::None) return {Decision,float(Frame)/FPS};
    }
    return {};
}

int32 During(FMCSlipperyRisk& Risk,int32 FPS,float Seconds,bool Allowed,bool Slippery,bool Moving,bool Carrying=false)
{
    int32 Decisions=0;
    for(int32 Frame=0;Frame<FMath::RoundToInt(Seconds*FPS);++Frame)
        Decisions+=Risk.Update(1.f/FPS,Allowed,Slippery,Moving,Carrying)!=EMCSlipperyDecision::None;
    return Decisions;
}

bool AtExpectedTime(const FDecisionAt& Observed,float Expected,int32 FPS)
{
    // A decision belongs to the first simulation frame crossing its deadline.
    // Permit one frame of quantization, not a proportional gameplay tolerance.
    return Observed.Decision!=EMCSlipperyDecision::None
        && FMath::Abs(Observed.Seconds-Expected)<=1.f/FPS+.0002f;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyCadenceTest,"MessControl.SlipperyRisk.MovingExposureCadenceAcrossFrameRates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyCadenceTest::RunTest(const FString&)
{
    TArray<float> Reference;
    for(int32 FPS:{30,60,120}) {
        FMCSlipperyRisk Risk;TArray<float> Rolls;int32 CarryFalls=0;
        for(int32 Frame=1;Frame<=FMath::RoundToInt(10.6f*FPS);++Frame) {
            const auto Decision=Risk.Update(1.f/FPS,true,true,true,false);
            if(Decision==EMCSlipperyDecision::Roll) Rolls.Add(float(Frame)/FPS);
            CarryFalls+=Decision==EMCSlipperyDecision::CarryFall;
            // Deliberately no OnFall: a successful recovery or blocked landing
            // still spends its opportunity and must wait the repeat window.
        }
        const FString Label=FString::Printf(TEXT("FPS=%d"),FPS);
        TestEqual(*(Label+TEXT(" produces four sampling opportunities, not one per rendered frame")),Rolls.Num(),4);
        TestEqual(*(Label+TEXT(" cannot request a carried fall without food")),CarryFalls,0);
        for(int32 Index=0;Index<Rolls.Num();++Index) {
            const float Expected=1.5f+3.f*Index;
            TestTrue(*(Label+FString::Printf(TEXT(" opportunity%d is at its elapsed deadline"),Index)),
                FMath::Abs(Rolls[Index]-Expected)<=1.f/FPS+.0002f);
            if(Reference.IsValidIndex(Index)) TestTrue(*(Label+TEXT(" keeps the same decision times across frame rates")),
                FMath::Abs(Rolls[Index]-Reference[Index])<=1.f/30+.0002f);
        }
        if(Reference.IsEmpty()) Reference=Rolls;
        AddInfo(FString::Printf(TEXT("FPS=%d movingSeconds=10.6 RollDecisions=%d frames=%d"),FPS,Rolls.Num(),FMath::RoundToInt(10.6f*FPS)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyPauseAndDryTest,"MessControl.SlipperyRisk.StationaryPauseDryDecayAndEligibilityReset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyPauseAndDryTest::RunTest(const FString&)
{
    using namespace MCSlipperyRiskTests;
    for(int32 FPS:{30,60,120}) {
        FMCSlipperyRisk Paused;
        TestEqual(TEXT("Initial partial exposure has no decision"),During(Paused,FPS,1.2f,true,true,true),0);
        TestEqual(TEXT("Standing on wet ground for ten seconds never accumulates a roll"),During(Paused,FPS,10.f,true,true,false),0);
        const auto Resume=Next(Paused,FPS,1.f);
        TestTrue(TEXT("Resuming movement keeps the paused exposure rather than restarting or advancing it"),AtExpectedTime(Resume,.3f,FPS));

        FMCSlipperyRisk Dry;
        During(Dry,FPS,1.2f,true,true,true);
        TestEqual(TEXT("A short dry interval cannot itself trigger a decision"),During(Dry,FPS,.2f,true,false,false),0);
        const auto AfterDry=Next(Dry,FPS,2.f);
        TestTrue(TEXT("Dry ground removes exposure at three seconds per second"),AtExpectedTime(AfterDry,.9f,FPS));
        TestEqual(TEXT("Fully drying out produces no decision"),During(Dry,FPS,2.f,true,false,false),0);
        const auto FreshEncounter=Next(Dry,FPS,2.f);
        TestTrue(TEXT("Fully dry ground restores the first encounter window, not the repeat window"),AtExpectedTime(FreshEncounter,1.5f,FPS));

        FMCSlipperyRisk Ineligible;
        During(Ineligible,FPS,1.2f,true,true,true);
        TestEqual(TEXT("Losing gameplay eligibility cannot trigger a fall"),During(Ineligible,FPS,1.f,false,true,true),0);
        const auto Reenabled=Next(Ineligible,FPS,2.f);
        TestTrue(TEXT("Regaining eligibility starts a fresh full window"),AtExpectedTime(Reenabled,1.5f,FPS));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyHitchTest,"MessControl.SlipperyRisk.HitchCannotBankRepeatedRandomOpportunities",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyHitchTest::RunTest(const FString&)
{
    using namespace MCSlipperyRiskTests;
    for(int32 FPS:{30,60,120}) {
        FMCSlipperyRisk Risk;
        TestEqual(TEXT("A long positive hitch issues one decision"),Risk.Update(30.f,true,true,true,false),EMCSlipperyDecision::Roll);
        const auto Following=Next(Risk,FPS,3.5f);
        TestEqual(TEXT("The next decision remains a roll opportunity"),Following.Decision,EMCSlipperyDecision::Roll);
        TestTrue(TEXT("Hitch overshoot cannot cause a next-frame or banked retry"),AtExpectedTime(Following,3.f,FPS));
        const auto AfterRecovery=Next(Risk,FPS,3.5f);
        TestTrue(TEXT("Not falling after that roll still consumes a whole repeat window"),AtExpectedTime(AfterRecovery,3.f,FPS));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyCarryRetryTest,"MessControl.SlipperyRisk.FoodPickupInterruptsExposureAndBlockedCarryRetries",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyCarryRetryTest::RunTest(const FString&)
{
    using namespace MCSlipperyRiskTests;
    for(int32 FPS:{30,60,120}) {
        FMCSlipperyRisk Risk;
        During(Risk,FPS,1.f,true,true,true);
        TestEqual(TEXT("Holding food while stationary cannot cause a fall"),During(Risk,FPS,2.f,true,true,false,true),0);
        TestEqual(TEXT("Moving with newly picked-up food requests an immediate carried fall"),
            Risk.Update(1.f/FPS,true,true,true,true),EMCSlipperyDecision::CarryFall);
        Risk.OnBlockedCarry();
        const auto Retry=Next(Risk,FPS,.5f,true);
        TestEqual(TEXT("A blocked carried fall retries the same carried action"),Retry.Decision,EMCSlipperyDecision::CarryFall);
        TestTrue(TEXT("Blocked placement waits a quarter second instead of retrying every frame"),AtExpectedTime(Retry,.25f,FPS));
        // Dropping food after the request must not reuse the pre-pickup exposure.
        const auto EmptyHanded=Next(Risk,FPS,2.f,false);
        TestEqual(TEXT("Dropping food restores an ordinary roll decision"),EmptyHanded.Decision,EMCSlipperyDecision::Roll);
        TestTrue(TEXT("The carry request spent the old unladen exposure"),AtExpectedTime(EmptyHanded,1.5f,FPS));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyCooldownTest,"MessControl.SlipperyRisk.FallGraceTicksWhileIneligibleAndDoesNotStackRetryDelay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyCooldownTest::RunTest(const FString&)
{
    using namespace MCSlipperyRiskTests;
    for(int32 FPS:{30,60,120}) {
        FMCSlipperyRisk Risk;Risk.OnFall();Risk.OnBlockedCarry();
        TestEqual(TEXT("Recovering/ineligible time causes no decisions"),During(Risk,FPS,3.f,false,true,true,true),0);
        const auto EligibleCarry=Next(Risk,FPS,4.f,true);
        TestEqual(TEXT("The first eligible carry after recovery requests a fall"),EligibleCarry.Decision,EMCSlipperyDecision::CarryFall);
        TestTrue(TEXT("Fall grace is 6.5 total seconds, including recovery, without adding the retry delay"),AtExpectedTime(EligibleCarry,3.5f,FPS));

        Risk.OnFall();
        TestEqual(TEXT("A full grace spent standing safely causes no decisions"),During(Risk,FPS,7.f,true,false,false),0);
        TestEqual(TEXT("Expired grace does not accumulate a backlog before the next moving carry"),
            Risk.Update(1.f/FPS,true,true,true,true),EMCSlipperyDecision::CarryFall);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyInputSanitizationTest,"MessControl.SlipperyRisk.InvalidElapsedTimeAndAuthoredLimits",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyInputSanitizationTest::RunTest(const FString&)
{
    using namespace MCSlipperyRiskTests;
    constexpr int32 FPS=60;
    const float NaN=std::numeric_limits<float>::quiet_NaN(),Infinity=std::numeric_limits<float>::infinity();
    FMCSlipperyRisk Accumulating;
    During(Accumulating,FPS,1.f,true,true,true);
    FMCSlipperyRisk Cooling;Cooling.OnFall();
    for(float Invalid:{0.f,-1.f,NaN,Infinity}) {
        TestEqual(TEXT("Invalid elapsed time cannot create a carried decision"),Accumulating.Update(Invalid,true,true,true,true),EMCSlipperyDecision::None);
        TestEqual(TEXT("Invalid elapsed time cannot advance cooldown decisions"),Cooling.Update(Invalid,true,true,true,true),EMCSlipperyDecision::None);
    }
    TestTrue(TEXT("Rejected elapsed values do not spend or advance prior exposure"),AtExpectedTime(Next(Accumulating,FPS,1.f),.5f,FPS));
    TestTrue(TEXT("Rejected elapsed values cannot consume the fall cooldown"),AtExpectedTime(Next(Cooling,FPS,7.f,true),6.5f,FPS));

    struct FLimitCase {float Authored,Expected;};
    for(const auto Case:{FLimitCase{-100.f,.25f},FLimitCase{0.f,.25f},FLimitCase{999.f,30.f},FLimitCase{NaN,1.5f},FLimitCase{Infinity,1.5f}}) {
        FMCSlipperyRisk Risk;
        const auto First=Next(Risk,FPS,31.f,false,Case.Authored,3.f);
        TestTrue(TEXT("The first authored window is clamped or safely defaulted"),AtExpectedTime(First,Case.Expected,FPS));
    }
    for(const auto Case:{FLimitCase{-100.f,.25f},FLimitCase{999.f,30.f},FLimitCase{NaN,3.f},FLimitCase{Infinity,3.f}}) {
        FMCSlipperyRisk Risk;
        Next(Risk,FPS,2.f);
        const auto Repeat=Next(Risk,FPS,31.f,false,1.5f,Case.Authored);
        TestTrue(TEXT("The repeat authored window is clamped or safely defaulted"),AtExpectedTime(Repeat,Case.Expected,FPS));
    }
    FMCSlipperyRisk InvalidGrace;InvalidGrace.OnFall(NaN);
    TestTrue(TEXT("An invalid fall-grace setting falls back to 6.5 seconds"),AtExpectedTime(Next(InvalidGrace,FPS,7.f,true),6.5f,FPS));
    FMCSlipperyRisk NegativeGrace;NegativeGrace.OnFall(-1.f);
    TestEqual(TEXT("A negative custom grace cannot create a negative timer"),NegativeGrace.Update(1.f/FPS,true,true,true,true),EMCSlipperyDecision::CarryFall);
    return true;
}
#endif
