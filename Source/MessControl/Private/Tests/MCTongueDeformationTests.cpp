#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCTongue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTongueCachedDeformationTest,"MessControl.Tongue.CachedDeformationMatchesCollisionSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTongueCachedDeformationTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
    auto* Tongue=World->SpawnActor<AMCTongue>();
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"));
#if WITH_EDITOR
    if (Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
    Tongue->SourceMesh=Mesh; Tongue->RebuildSurface(); Tongue->Settings.Sanitize();
    if (!TestTrue(TEXT("Uses the authored collision surface"),Tongue->Rest.Num()>100))
    { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false; }
    TArray<FMCTongueMotionState> Pulses;
    FMCTongueMotionState Pulse; Pulse.Settings.Shape=EMCTongueShape::RadialWave; Pulse.Settings.Height=27;
    Pulse.Settings.Redness=.7f; Pulse.StartedAt=1; Pulse.Origin=Tongue->RestBounds.GetCenter(); Pulses.Add(Pulse);
    Tongue->YawnStartedAt=.2; Tongue->YawnDuration=4;
    float MaxHeightError=0,MaxColorError=0;
    const EMCTongueShape Shapes[]={EMCTongueShape::FrontBend,EMCTongueShape::RadialWave,EMCTongueShape::LocalLift,EMCTongueShape::DirectionalWave};
    const float Times[]={-.1f,.1f,.7f,1.5f,3.f,8.f,300.f};
    for (int32 Pass=0;Pass<2;++Pass)
    {
        Tongue->SetActorTransform(Pass?FTransform(FRotator(12,35,-7),FVector(200,-340,70),FVector(1.2,.8,1.4)):FTransform::Identity);
        Tongue->RefreshDeformationMotion();
        bool TransformRefreshed=true;
        for (int32 I=0;I<Tongue->DeformationSamples.Num();I+=107)
        {
            const auto& Sample=Tongue->DeformationSamples[I];
            TransformRefreshed &= FMath::IsNearlyEqual(Sample.Distance,Tongue->MotionDistance(Sample.Point),.001f)
                && FMath::IsNearlyEqual(Sample.MotionMask,Tongue->MotionWeight(Sample.Point),.00001f);
        }
        TestTrue(TEXT("Moving/scaling the tongue invalidates samples without a new motion serial"),TransformRefreshed);
        for (EMCTongueShape Shape:Shapes)
        {
            Tongue->Motion.Settings.Shape=Shape; Tongue->Motion.Settings.Redness=.8f;
            Tongue->Motion.Origin=Tongue->RestBounds.GetCenter()+FVector(25,-40,0);
            Tongue->Motion.Direction=FVector(.6,.8,0); Tongue->Motion.StartedAt=.1; ++Tongue->Motion.Serial;
            Tongue->RefreshDeformationMotion();
            for (float Time:Times)
            {
                const float Idle=Time*2*PI/Tongue->Settings.IdlePeriod;
                const float Envelope=Tongue->Motion.Settings.Envelope(Time-Tongue->Motion.StartedAt);
                const float Yawn=Time>=Tongue->YawnStartedAt && Time<Tongue->YawnStartedAt+Tongue->YawnDuration
                    ?45*FMath::Sin(PI*(Time-Tongue->YawnStartedAt)/FMath::Max(1.f,Tongue->YawnDuration)):0;
                for (int32 I=0;I<Tongue->DeformationSamples.Num();I+=17)
                {
                    const auto& Sample=Tongue->DeformationSamples[I]; float A=0,B=0;
                    const float Expected=Tongue->Offset(Sample.Point,Time,A,Pulses);
                    const float Actual=Tongue->SampleOffset(Sample,Time,Idle,Envelope,Yawn,B,Pulses);
                    MaxHeightError=FMath::Max(MaxHeightError,FMath::Abs(Expected-Actual));
                    MaxColorError=FMath::Max(MaxColorError,FMath::Abs(A-B));
                }
            }
        }
    }
    TestTrue(TEXT("Every displacement/normal sample preserves motion, yawn and overlapping ulcers"),MaxHeightError<.001f);
    TestTrue(TEXT("The pressure/event material mask is preserved"),MaxColorError<.00001f);
    // A replicated reset+start can reuse a serial without the client observing
    // the neutral state. Its geometry still has to refresh immediately.
    Tongue->Motion.Origin+=FVector(130,-280,0);Tongue->Motion.Direction=FVector(0,-1,0);
    Tongue->Motion.Settings.Shape=EMCTongueShape::LocalLift;Tongue->Motion.Settings.Radius=370;
    Tongue->RefreshDeformationMotion();
    bool ReusedSerialRefreshed=true;
    for (int32 I=0;I<Tongue->DeformationSamples.Num();I+=107)
    {
        const auto& Sample=Tongue->DeformationSamples[I];
        ReusedSerialRefreshed &= FMath::IsNearlyEqual(Sample.Distance,Tongue->MotionDistance(Sample.Point),.001f)
            && FMath::IsNearlyEqual(Sample.MotionMask,Tongue->MotionWeight(Sample.Point),.00001f);
    }
    TestTrue(TEXT("A reused motion serial refreshes the origin, shape, radius and direction"),ReusedSerialRefreshed);
    Tongue->Motion=FMCTongueMotionState(); Tongue->RefreshDeformationMotion(); Pulses.Reset();
    float Sum=0,Red=0;
    const double ReferenceStart=FPlatformTime::Seconds();
    for (int32 Repeat=0;Repeat<4;++Repeat) for (const auto& Sample:Tongue->DeformationSamples)
        Sum+=Tongue->Offset(Sample.Point,8,Red,Pulses);
    const double ReferenceMs=(FPlatformTime::Seconds()-ReferenceStart)*250;
    const double CachedStart=FPlatformTime::Seconds();
    const float Idle=8.f*2*PI/Tongue->Settings.IdlePeriod;
    for (int32 Repeat=0;Repeat<4;++Repeat) for (const auto& Sample:Tongue->DeformationSamples)
        Sum+=Tongue->SampleOffset(Sample,8,Idle,0,0,Red,Pulses);
    const double CachedMs=(FPlatformTime::Seconds()-CachedStart)*250;
    AddInfo(FString::Printf(TEXT("Authored tongue %d samples; displacement reference %.3f ms, cached %.3f ms; max height error %.8f; checksum %.3f"),Tongue->DeformationSamples.Num(),ReferenceMs,CachedMs,MaxHeightError,Sum));
    World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
