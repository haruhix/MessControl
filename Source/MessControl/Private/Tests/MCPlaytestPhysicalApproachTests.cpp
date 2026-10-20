#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPlaytestPhysicalApproach.h"
#include "MCPlaytestBotController.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"

namespace MCPlaytestPhysicalApproachTestsPrivate
{
/** No navmesh or bot backdoor: the final segment uses the same collision
 * planner as the AI, then ordinary input and real CharacterMovement ticks. */
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    UBoxComponent* Floor=nullptr;
    explicit FFixture(float FloorPitch=0.f)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Floor=Box(FVector(0,0,-20),FVector(1000,1000,20));
        Floor->GetOwner()->SetActorRotation(FRotator(FloorPitch,0,0));
        const float Half=GetDefault<AMCToothCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,Half+4),FRotator::ZeroRotator);
        // A headless PlayerController has no ULocalPlayer and is therefore
        // nonlocal in UE 5.8's no-NetDriver path. A real local authority AI is
        // the production context for this fallback, without bot task timers.
        World->SpawnActor<AAIController>()->Possess(Hero);
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        for(int32 I=0;I<20;++I) Frame();
    }
    ~FFixture() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    UBoxComponent* Box(FVector Center,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Collision=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Collision);Collision->SetBoxExtent(Extent);Collision->SetCollisionProfileName(TEXT("BlockAll"));
        Collision->RegisterComponent();Actor->SetActorLocation(Center);return Collision;
    }
    void Frame() {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestPhysicalApproachSafety,
    "MessControl.PlaytestBots.PhysicalApproach.RechecksWallsSupportAndDistance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestPhysicalApproachSafety::RunTest(const FString&)
{
    using namespace MCPlaytestPhysicalApproach;
    MCPlaytestPhysicalApproachTestsPrivate::FFixture F;
    if(!TestTrue(TEXT("Real authority AI ownership permits native controlled CMC movement"),
        Cast<AAIController>(F.Hero->GetController()) && F.Hero->IsLocallyControlled())) return false;
    const FVector Start=F.Hero->GetActorLocation();
    FVector End;
    if(!TestTrue(TEXT("A real supported endpoint exists without Nav"),FindStandingPose(F.Hero,Start+FVector(160,0,0),End))) return false;
    TestTrue(TEXT("A supported capsule-clear short segment is physically legal"),CanWalkSegment(F.Hero,Start,End));
    TestFalse(TEXT("The fallback never becomes arbitrary off-Nav travel"),CanWalkSegment(F.Hero,Start,Start+FVector(201,0,0)));
    auto* Wall=F.Box(FVector(80,0,130),FVector(8,150,150));
    F.Hero->ConsumeMovementInputVector();
    TestFalse(TEXT("A new real wall invalidates the full capsule segment"),CanWalkSegment(F.Hero,Start,End));
    TestFalse(TEXT("Blocked travel does not submit movement"),SubmitWalkInput(F.Hero,End));
    TestTrue(TEXT("No input attempts to push through the blocked route"),F.Hero->GetPendingMovementInputVector().IsNearlyZero());
    Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // A supported endpoint beyond a gap is insufficient: the entire segment
    // and capsule footprint must stay supported, including after planning.
    F.Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    F.Box(FVector(-50,0,-20),FVector(95,250,20));
    F.Box(FVector(210,0,-20),FVector(95,250,20));
    FVector SupportedStart;
    TestTrue(TEXT("The origin retains its real floor"),FindStandingPose(F.Hero,Start,SupportedStart));
    TestTrue(TEXT("The destination still has its real floor"),FindStandingPose(F.Hero,End,End));
    TestFalse(TEXT("A missing intermediate floor invalidates the segment"),CanWalkSegment(F.Hero,Start,End));
    TestFalse(TEXT("Floor loss prevents new input before the pawn steps off"),SubmitWalkInput(F.Hero,End));
    TestTrue(TEXT("Floor loss leaves no latent physical-walk input"),F.Hero->GetPendingMovementInputVector().IsNearlyZero());
    {
        MCPlaytestPhysicalApproachTestsPrivate::FFixture Inclined(35.f);
        FVector SlopeStart,SlopeEnd;
        TestTrue(TEXT("A normal walkable incline predicts the real capsule stance"),FindStandingPose(Inclined.Hero,Inclined.Hero->GetActorLocation(),SlopeStart));
        TestTrue(TEXT("The inclined final endpoint retains footprint support"),FindStandingPose(Inclined.Hero,SlopeStart+FVector(120,0,0),SlopeEnd));
        TestTrue(TEXT("A clear ordinary slope is not rejected as floor penetration"),CanWalkSegment(Inclined.Hero,SlopeStart,SlopeEnd));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestPhysicalApproachActualWalk,
    "MessControl.PlaytestBots.PhysicalApproach.WalksThroughOrdinaryCMCInput",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestPhysicalApproachActualWalk::RunTest(const FString&)
{
    using namespace MCPlaytestPhysicalApproach;
    MCPlaytestPhysicalApproachTestsPrivate::FFixture F;
    if(!TestTrue(TEXT("A real authority AI drives the controlled native movement branch"),
        Cast<AAIController>(F.Hero->GetController()) && F.Hero->IsLocallyControlled())) return false;
    const FVector Start=F.Hero->GetActorLocation();
    const float Health=F.Hero->Status->State.Health;
    FVector End;
    if(!TestTrue(TEXT("The real floor supports the short final stance"),FindStandingPose(F.Hero,Start+FVector(160,0,0),End))) return false;
    bool Accepted=true,Accelerated=false,Continuous=true;
    FVector Previous=Start;
    for(int32 I=0;I<180 && FVector::Dist2D(F.Hero->GetActorLocation(),End)>20.f;++I) {
        Accepted&=SubmitWalkInput(F.Hero,End);F.Frame();
        const FVector Position=F.Hero->GetActorLocation();
        Accelerated|=F.Hero->GetCharacterMovement()->GetCurrentAcceleration().X>1;
        Continuous&=!Position.ContainsNaN() && FVector::Dist(Position,Previous)<40.f;Previous=Position;
    }
    F.Hero->ConsumeMovementInputVector();
    TestTrue(TEXT("Every movement frame retained real floor and capsule clearance"),Accepted);
    TestTrue(TEXT("The helper supplied actual CMC acceleration"),Accelerated);
    TestTrue(TEXT("Ordinary physics moved the worker across the final segment"),F.Hero->GetActorLocation().X-Start.X>100.f);
    TestTrue(TEXT("The worker reached the physical cleaning stance without placement"),FVector::Dist2D(F.Hero->GetActorLocation(),End)<=20.f);
    TestTrue(TEXT("The movement stayed finite and continuous"),Continuous);
    TestTrue(TEXT("The ordinary worker remains alive"),F.Hero->Status->IsAlive());
    TestEqual(TEXT("The helper supplies no healing or damage exemption"),F.Hero->Status->State.Health,Health);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestPhysicalToothTask,
    "MessControl.PlaytestBots.PhysicalApproach.ControllerCleansAcrossShortLegWithoutNavPrefix",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestPhysicalToothTask::RunTest(const FString&)
{
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("The tooth fixture has a real collidable surface mesh"),Cube)) return false;
    for(const bool Blocked:{false,true}) {
        MCPlaytestPhysicalApproachTestsPrivate::FFixture F;
        auto* PreviousController=F.Hero->GetController();
        auto* Bot=F.World->SpawnActor<AMCPlaytestBotController>();
        Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);Bot->Possess(F.Hero);Bot->SetLabControlled(true);
        if(IsValid(PreviousController)) PreviousController->Destroy();
        const FTransform Transform(FVector(280,0,100));
        auto* Tooth=F.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Transform);
        if(!TestNotNull(TEXT("The work target is an ordinary arena tooth"),Tooth)) return false;
        Tooth->SetAppearance(Cube,FVector(.8f,2,2));Tooth->FinishSpawning(Transform);
        FMCArenaToothSettings Settings;Settings.bPianoEnabled=false;Settings.InitialCalculusEveryNthTooth=0;
        Tooth->Initialize(1,Settings);Tooth->SetCoffee(1);
        for(int32 I=0;I<20;++I) F.Frame();
        TArray<FVector> Samples,Normals;Tooth->GetDirtyContactSamples(Samples,Normals);
        if(!TestTrue(TEXT("The ordinary coffee creates real dirty contact samples"),Tooth->Status->NeedsCare(true) && !Samples.IsEmpty())) return false;
        FNavLocation Projected;auto* Nav=UNavigationSystemV1::GetCurrent(F.World);
        TestFalse(TEXT("The fixture provides no navigable prefix to conceal the regression"),
            Nav && Nav->ProjectPointToNavigation(F.Hero->GetActorLocation(),Projected,FVector(65,65,200),&F.Hero->GetNavAgentPropertiesRef()));
        FVector Contact,Normal;
        TestFalse(TEXT("The dirty crown initially lies outside real brush contact"),Tooth->FindDirtyContact(F.Hero,Contact,Normal));
        if(Blocked) F.Box(FVector(150,0,130),FVector(10,500,150));
        const FVector Start=F.Hero->GetActorLocation();const float InitialGrime=Tooth->RemainingGrime();
        const int32 InitialContacts=F.Hero->SuccessfulBrushContacts;
        Bot->DriveLabTask(Tooth,EMCPlaytestBotGoal::Clean);
        TestEqual(TEXT("Choosing an approach cannot erase dirt before native tool ticks"),Tooth->RemainingGrime(),InitialGrime);
        if(Blocked) {
            for(int32 I=0;I<30;++I) F.Frame();
            TestTrue(TEXT("A real intervening wall prevents the controller's physical leg"),FVector::Dist2D(Start,F.Hero->GetActorLocation())<2.f);
            TestEqual(TEXT("A blocked task creates no successful brush contacts"),F.Hero->SuccessfulBrushContacts,InitialContacts);
            TestEqual(TEXT("The wall preserves the actual stain mask"),Tooth->RemainingGrime(),InitialGrime);
            TestTrue(TEXT("No blocked direct-walk input remains queued"),F.Hero->GetPendingMovementInputVector().IsNearlyZero());
            continue;
        }
        bool SawPhysicalChoice=false,SawAcceleration=false;
        for(int32 I=0;I<900 && F.Hero->SuccessfulBrushContacts==InitialContacts;++I) {
            Bot->DriveLabTask(Tooth,EMCPlaytestBotGoal::Clean);
            SawPhysicalChoice|=Bot->GetActivity()==TEXT("physical_tooth_approach");
            F.Frame();SawAcceleration|=F.Hero->GetCharacterMovement()->GetCurrentAcceleration().X>1;
        }
        TestTrue(TEXT("The real controller selects the short physical approach without a Nav prefix"),SawPhysicalChoice);
        TestTrue(TEXT("The selected task supplies ordinary CMC acceleration"),SawAcceleration);
        TestTrue(TEXT("The worker actually walks into native brush reach"),F.Hero->GetActorLocation().X-Start.X>20.f);
        TestTrue(TEXT("Native brush contacts remove real coating layers"),F.Hero->SuccessfulBrushContacts>InitialContacts);
        TestTrue(TEXT("The actual remaining surface grime decreases"),Tooth->RemainingGrime()<InitialGrime);
        Bot->SetLabControlled(true);
        TestTrue(TEXT("Ending the explicit task releases physical movement input"),F.Hero->GetPendingMovementInputVector().IsNearlyZero());
        TestFalse(TEXT("Ending the explicit task releases primary input"),F.Hero->IsPrimaryHeld());
    }
    return true;
}
#endif
