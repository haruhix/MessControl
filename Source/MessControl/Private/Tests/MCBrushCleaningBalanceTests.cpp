#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCoffeeWipe.h"
#include "MCSurfaceWipe.h"

namespace MCBrushCleaningBalanceTests
{
struct FDuration {float Seconds=0;int32 Steps=0;};

FDuration PuddleContact(float Dt,float ToolRate)
{
    TArray<uint8> Mask;TArray<float> Fractional;FMCCoffeeWipe::Reset(Mask);
    // One unchanged local trajectory through an actual mask cell. Do not
    // substitute a global completion timer or change the footprint for speed.
    const FVector2D At(32.5/64.,32.5/64.);const int32 Probe=32*64+32;
    FDuration Result;
    while(Mask[Probe]>63 && Result.Seconds<2.f) {
        FMCCoffeeWipe::Stroke(Mask,At,At,.12f,Dt,&Fractional,ToolRate);
        Result.Seconds+=Dt;++Result.Steps;
    }
    return Result;
}

FDuration ToothContact(float Dt,float ToolRate)
{
    TArray<uint8> Mask;TArray<float> Precise;FMCSurfaceWipe::Reset(Mask);
    const FVector At=FVector(8,8,8)/15.;const int32 Probe=FMCSurfaceWipe::Index(8,8,8);
    FDuration Result;
    while(Mask[Probe]>63 && Result.Seconds<2.f) {
        // Arena brushing retains its existing two-exposure stroke convention.
        FMCSurfaceWipe::Stroke(Mask,At,At,FVector(200),45.f,Dt*2,&Precise,ToolRate);
        Result.Seconds+=Dt;++Result.Steps;
    }
    return Result;
}

// The previous behaviour is a reference exposure, not the new balance constant.
// Stop at the same visible working-residue threshold as the real mask solvers.
FDuration LegacyContact(float Dt,float ToolRate,bool Tooth)
{
    float Coverage=255;FDuration Result;
    while(FMath::FloorToInt(Coverage)>63 && Result.Seconds<2.f) {
        if(Tooth) Coverage=FMath::Max(0.f,Coverage-950.f*FMath::Min(2*Dt,.1f)*ToolRate);
        else Coverage*=FMath::Exp(-12.f*FMath::Min(Dt,.1f)*ToolRate);
        Result.Seconds+=Dt;++Result.Steps;
    }
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushContactDurationTest,"MessControl.Cleanup.BrushBalance.SamePathNeedsLessContactTime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushContactDurationTest::RunTest(const FString&)
{
    using namespace MCBrushCleaningBalanceTests;
    for(float Dt:{1.f/30,1.f/60,1.f/120}) for(float ToolRate:{1.f,2.f}) for(bool Tooth:{false,true}) {
        const FDuration Previous=LegacyContact(Dt,ToolRate,Tooth);
        const FDuration Current=Tooth?ToothContact(Dt,ToolRate):PuddleContact(Dt,ToolRate);
        const FString Label=FString::Printf(TEXT("%s toolRate=%.0f FPS=%.0f"),Tooth?TEXT("Tooth"):TEXT("Puddle"),ToolRate,1/Dt);
        AddInfo(FString::Printf(TEXT("%s previous=%.6fs/%d contacts current=%.6fs/%d contacts"),
            *Label,Previous.Seconds,Previous.Steps,Current.Seconds,Current.Steps));
        TestTrue(*(Label+TEXT(" reaches the actual dry threshold")),Current.Seconds<2.f);
        // Discrete accepted contacts may round both versions to the same final
        // frame. Permit one frame of quantisation, not a widened gameplay goal.
        TestTrue(*(Label+TEXT(" reaches about 60 percent of former contact duration")),
            FMath::Abs(Current.Seconds-.6f*Previous.Seconds)<=Dt*1.01f);
        TestTrue(*(Label+TEXT(" never requires more accepted contacts")),Current.Steps<=Previous.Steps);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushLocalityAndFractionalTest,"MessControl.Cleanup.BrushBalance.LocalityFrameRateAndToolCap",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushLocalityAndFractionalTest::RunTest(const FString&)
{
    const FVector At=FVector(8,8,0)/15.;const int32 ToothProbe=FMCSurfaceWipe::Index(8,8,0);
    const FVector2D UV(32.5/64.,32.5/64.);const int32 PuddleProbe=32*64+32;
    TArray<uint8> Tooth30,Tooth120,Floor30,Floor120;
    for(int32 FPS:{30,120}) {
        TArray<uint8> Tooth,Floor;TArray<float> TP,FP;
        FMCSurfaceWipe::Reset(Tooth);FMCCoffeeWipe::Reset(Floor);
        // Equal short exposures stay below saturation so fractional accuracy
        // and boost ratio remain observable, rather than every cell becoming 0.
        for(int32 Step=0;Step<FPS/10;++Step) {
            FMCSurfaceWipe::Stroke(Tooth,At,At,FVector(200),45.f,1.f/FPS,&TP);
            FMCCoffeeWipe::Stroke(Floor,UV,UV,.12f,1.f/FPS,&FP);
        }
        TestTrue(TEXT("Accepted exposure actually cleans the contacted enamel"),Tooth[ToothProbe]<255);
        TestTrue(TEXT("Accepted exposure actually cleans the contacted floor"),Floor[PuddleProbe]<255);
        TestEqual(TEXT("A stronger dose cannot jump to the opposite tooth face"),Tooth[FMCSurfaceWipe::Index(8,8,15)],uint8(255));
        TestEqual(TEXT("A stronger dose does not broaden the floor footprint"),Floor[2*64+2],uint8(255));
        const auto Before=Floor;
        TestFalse(TEXT("Out-of-area brush requests still do no work"),FMCCoffeeWipe::Stroke(Floor,{-2,-2},{-1,-1},.12f,.1f,&FP));
        TestTrue(TEXT("Rejected requests preserve existing work"),Floor==Before);
        if(FPS==30) {Tooth30=MoveTemp(Tooth);Floor30=MoveTemp(Floor);}
        else {Tooth120=MoveTemp(Tooth);Floor120=MoveTemp(Floor);}
    }
    TestTrue(TEXT("Actual tooth dose is stable across 30 and 120 FPS"),FMath::Abs(int32(Tooth30[ToothProbe])-int32(Tooth120[ToothProbe]))<=1);
    TestTrue(TEXT("Actual floor dose is stable across 30 and 120 FPS"),FMath::Abs(int32(Floor30[PuddleProbe])-int32(Floor120[PuddleProbe]))<=1);
    TArray<uint8> Normal,Fast,Capped;TArray<float> NP,FP,CP;
    for(auto* Mask:{&Normal,&Fast,&Capped}) FMCSurfaceWipe::Reset(*Mask);
    FMCSurfaceWipe::Stroke(Normal,At,At,FVector(200),45.f,.01f,&NP,1.f);
    FMCSurfaceWipe::Stroke(Fast,At,At,FVector(200),45.f,.01f,&FP,2.f);
    FMCSurfaceWipe::Stroke(Capped,At,At,FVector(200),45.f,.01f,&CP,200.f);
    TestTrue(TEXT("The ordinary tool upgrade still doubles unsaturated accepted work"),FMath::IsNearlyEqual(255-FP[ToothProbe],2*(255-NP[ToothProbe]),.001f));
    TestTrue(TEXT("The existing tool-rate cap survives the baseline dose change"),Fast==Capped);
    return true;
}
#endif
