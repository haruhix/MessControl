#include "MCGlideAnimation.h"
#include "MCPhysicsTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGlideOnsetTest,"MessControl.Animation.AuthoredGlide.OnsetsAndCooldown",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGlideOnsetTest::RunTest(const FString&)
{
    constexpr float Balance=38.f/24.f;
    FMCGlideAnimationState State;
    State.Update(.1f,true,false,200.f,0.f,0.f,Balance);
    TestEqual(TEXT("Ordinary ground does not start a slide"),State.Sample.Alpha,0.f);
    State.Update(.1f,true,true,200.f,0.f,0.f,Balance);
    TestTrue(TEXT("Entering slippery ground at speed plays the artist balance clip"),State.Sample.Alpha>.8f && !State.Sample.bStrong);
    for(int32 I=0;I<220;++I) State.Update(1.f/60.f,true,true,200.f,0.f,0.f,Balance);
    TestEqual(TEXT("Steady travel never loops the completed balance reaction"),State.Sample.Alpha,0.f);
    State.Update(.04f,true,true,300.f,.85f,0.f,Balance);
    TestTrue(TEXT("A fast sideways skid still uses the balance attempt, never the fall clip"),!State.Sample.bStrong && State.Sample.Alpha>0.f);
    TestEqual(TEXT("The entire authored balance clip remains available"),State.Duration,Balance);
    State.Update(.1f,false,true,300.f,.85f,0.f,Balance);
    TestEqual(TEXT("A high-priority action suppresses the cosmetic clip immediately"),State.Sample.Alpha,0.f);
    State.Update(.1f,true,true,300.f,.85f,0.f,Balance);
    TestEqual(TEXT("Releasing the action during the same skid cannot bypass cooldown"),State.Sample.Alpha,0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGlideReleaseTest,"MessControl.Animation.AuthoredGlide.ReleaseAndMissingAssets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGlideReleaseTest::RunTest(const FString&)
{
    constexpr float Balance=38.f/24.f;
    FMCGlideAnimationState State;
    State.Update(.1f,true,true,120.f,0.f,0.f,Balance);
    TestEqual(TEXT("A slow surface contact has no authored skid"),State.Sample.Alpha,0.f);
    State.Update(.1f,true,true,180.f,0.f,0.f,Balance);
    TestTrue(TEXT("Accelerating from rest on ice also starts the artist clip"),State.Sample.Alpha>0.f);
    State=FMCGlideAnimationState();
    State.Update(.1f,true,false,200.f,0.f,0.f,Balance);
    State.Update(.2f,true,true,200.f,0.f,0.f,Balance);
    TestTrue(TEXT("The balance attempt reaches full blend"),State.Sample.Alpha>.99f);
    State.Update(.04f,true,false,200.f,0.f,0.f,Balance);
    TestTrue(TEXT("Leaving the surface fades rather than holding the final pose"),State.Sample.Alpha>0.f && State.Sample.Alpha<1.f);
    State.Update(.16f,true,false,200.f,0.f,0.f,Balance);
    TestEqual(TEXT("Release returns to ordinary locomotion"),State.Sample.Alpha,0.f);
    FMCGlideAnimationState Missing;
    Missing.Update(.1f,true,true,300.f,1.f,600.f,0.f);
    TestEqual(TEXT("A missing balance asset never falls back to a failed-balance fall"),Missing.Sample.Alpha,0.f);
    FMCGlideAnimationState HardBrake;
    HardBrake.Update(.1f,true,true,420.f,1.f,900.f,Balance);
    TestTrue(TEXT("Even the strongest skid cannot consume the reserved fall clip"),HardBrake.Sample.Alpha>0.f && !HardBrake.Sample.bStrong);
    TestEqual(TEXT("A hard brake retains the full balance-attempt timing"),HardBrake.Duration,Balance);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFallGateTest,"MessControl.Animation.AuthoredGlide.AuthoritativeFallGate",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFallGateTest::RunTest(const FString&)
{
    TestEqual(TEXT("Standing keeps its saved network value"),uint8(EMCBodyState::Standing),uint8(0));
    TestEqual(TEXT("Ragdoll keeps its saved network value"),uint8(EMCBodyState::Ragdoll),uint8(1));
    TestEqual(TEXT("Recovering keeps its saved network value"),uint8(EMCBodyState::Recovering),uint8(2));
    TestEqual(TEXT("Slipping is appended to the body-state protocol"),uint8(EMCBodyState::Slipping),uint8(3));
    FMCSlipperyFallGate Gate;
    for(int32 I=0;I<20;++I) TestFalse(TEXT("Even a severe entry first allows a balance attempt"),Gate.Update(.02f,true,true,360.f,.9f,0.f));
    bool Fell=false;
    for(int32 I=0;I<10;++I) Fell|=Gate.Update(.02f,true,true,360.f,.9f,0.f);
    TestTrue(TEXT("Sustained severe skidding escalates to a real fall"),Fell);
    for(int32 I=0;I<250;++I) TestFalse(TEXT("Cooldown protects the fall/slide/rise and immediate next step"),Gate.Update(.02f,true,true,360.f,.9f,0.f));
    FMCSlipperyFallGate Controlled;
    for(int32 I=0;I<400;++I) TestFalse(TEXT("Straight controlled movement never falls from surface alone"),Controlled.Update(.02f,true,true,360.f,.1f,0.f));
    FMCSlipperyFallGate Contact;
    for(int32 I=0;I<20;++I) Contact.Update(.02f,true,true,360.f,.9f,0.f);
    Contact.Update(.02f,false,true,360.f,.9f,0.f);
    TestEqual(TEXT("A priority contact clears preparation before the fall"),Contact.BalanceAge,0.f);
    TestEqual(TEXT("A priority contact clears severe-skid accumulation"),Contact.SevereAge,0.f);
    return true;
}
#endif
