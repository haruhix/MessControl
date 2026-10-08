#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCNutLandingShadow.h"
#include "MCNutRainEvent.h"
#include "MCNutRainProfile.h"
#include "MCTongue.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCNutLandingTestsPrivate
{
    struct FWorldFixture
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        AMCTongue* Tongue=nullptr;
        AMCNutRainEvent* Event=nullptr;
        UMCNutRainProfile* Profile=nullptr;
        FWorldFixture()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
            auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
            const FTransform Pose(FRotator::ZeroRotator,FVector::ZeroVector,FVector(24,16,.2));
            Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose);
            Tongue->SourceMesh=Cube; Tongue->bAutomaticYawns=false;
            Tongue->FinishSpawning(Pose); Tongue->SetActorTickEnabled(false);
            Profile=NewObject<UMCNutRainProfile>(World);
            Profile->Settings.bBossEncounter=false;
            Profile->Settings.NutCount=10; Profile->Settings.NutsPerExtraPlayer=0;
            Profile->Settings.RainSeconds=3; Profile->Settings.Entry.FlightSeconds=.75f;
            Profile->Settings.Entry.EntryHeight=150;
            auto* Menu=NewObject<UDataTable>(World); Menu->RowStruct=FMCFoodRow::StaticStruct();
            FMCFoodRow Row; Row.WholeMeshes={Cube}; Row.FragmentMeshes={Cube}; Row.Scale=FVector(.5);
            Row.FragmentScale=FVector(.25); Row.Resistance=EMCFoodResistance::Hard; Row.SpoilSeconds=600;
            Menu->AddRow(TEXT("Walnut"),Row); Profile->Menu=Menu;
            Event=World->SpawnActor<AMCNutRainEvent>(); Event->Start(nullptr,Profile);
        }
        ~FWorldFixture()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
        void Step(float Seconds)
        {
            for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) {
                ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutLandingShadowLifecycleTest,"MessControl.CoreLoop.NutRain.PreimpactShadowAndReset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutLandingShadowLifecycleTest::RunTest(const FString&)
{
    MCNutLandingTestsPrivate::FWorldFixture T;
    if(!TestEqual(TEXT("The first airborne nut has a landing warning"),T.Event->LandingShadows.Num(),1)) return false;
    auto* Warning=T.Event->LandingShadows[0].Get();
    TestTrue(TEXT("Landing warning is active before impact"),Warning->IsWarningActive());
    TestTrue(TEXT("The warning retains the authored flight reaction time"),Warning->SecondsToImpact()>.7f);
    TestTrue(TEXT("Landing cue follows the same tongue as the rain"),Warning->Tongue==T.Tongue);
    T.Step(.15f);
    TestTrue(TEXT("A soft cue fades in before the nut arrives"),Warning->GetShadowStrength()>0);
    TestTrue(TEXT("The cue remains faint"),Warning->GetShadowStrength()<=T.Profile->Settings.LandingShadowOpacity);
    FHitResult Floor;
    TestTrue(TEXT("The warning is supported on the real live tongue"),T.Tongue->SurfacePoint(Warning->GetActorLocation(),Floor));
    TestTrue(TEXT("Decal projection remains close to the current tongue surface"),FVector::Dist(Warning->GetActorLocation(),Floor.ImpactPoint)<5);
    const FVector Landing=T.Event->PlannedLandings[0];
    TestTrue(TEXT("Warning identifies the planned ballistic landing footprint"),FVector::DistSquared2D(Warning->GetActorLocation(),Landing)<1);
    TArray<UPrimitiveComponent*> CollisionComponents; Warning->GetComponents(CollisionComponents);
    TestEqual(TEXT("Warnings have no collision components that could block a dodge"),CollisionComponents.Num(),0);
    T.Event->Stop();
    int32 Remaining=0;
    for(TActorIterator<AMCNutLandingShadow> It(T.World);It;++It) if(!It->IsActorBeingDestroyed()) ++Remaining;
    TestEqual(TEXT("Stopping the storm removes every owned landing warning"),Remaining,0);
    TestEqual(TEXT("Stopping clears warning handles"),T.Event->LandingShadows.Num(),0);
    TestEqual(TEXT("Stopping clears the seeded emission schedule"),T.Event->PlannedLaunchTimes.Num(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutRainSeededChaosTest,"MessControl.CoreLoop.NutRain.SeededBurstsAndVariedTrajectories",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutRainSeededChaosTest::RunTest(const FString&)
{
    MCNutLandingTestsPrivate::FWorldFixture A,B;
    TestEqual(TEXT("The emission budget stays unchanged"),A.Event->PlannedLaunchTimes.Num(),10);
    TestTrue(TEXT("The same seed recreates the same irregular timing plan"),A.Event->PlannedLaunchTimes==B.Event->PlannedLaunchTimes);
    const auto& Times=A.Event->PlannedLaunchTimes;
    if(Times.Num()!=10) return false;
    TestEqual(TEXT("The first nut arrives immediately"),Times[0],0.f);
    TestEqual(TEXT("The final launch keeps the authored storm duration"),Times.Last(),3.f,.0001f);
    float MinGap=FLT_MAX,MaxGap=0;
    for(int32 I=1;I<Times.Num();++I) {
        const float Gap=Times[I]-Times[I-1];
        TestTrue(TEXT("Emission times are ordered without negative gaps"),Gap>0);
        MinGap=FMath::Min(MinGap,Gap); MaxGap=FMath::Max(MaxGap,Gap);
    }
    TestTrue(TEXT("Bursts and gaps replace the old evenly timed cadence"),MaxGap>MinGap*2);
    A.Step(3.15f);
    TestEqual(TEXT("Chaotic timing emits exactly the configured number of nuts"),A.Event->NutsSpawned,10);
    TestEqual(TEXT("Every launch records a valid landing target"),A.Event->PlannedLandings.Num(),10);
    bool DifferentDirection=false;
    for(int32 I=1;I<A.Event->LaunchLocations.Num();++I) {
        const FVector First=(A.Event->PlannedLandings[0]-A.Event->LaunchLocations[0]).GetSafeNormal2D();
        const FVector Current=(A.Event->PlannedLandings[I]-A.Event->LaunchLocations[I]).GetSafeNormal2D();
        DifferentDirection|=FVector::DotProduct(First,Current)<.8f;
    }
    TestTrue(TEXT("Nuts approach in multiple directions instead of a single centered stream"),DifferentDirection);
    return true;
}
#endif
