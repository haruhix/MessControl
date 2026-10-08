#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFogBrawlEvent.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "ProceduralMeshComponent.h"

namespace MCFogBrawlTestsPrivate
{
struct FFogWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCFogBrawlEvent* Event=nullptr;
    AMCArenaTooth* Tooth=nullptr;
    TArray<AMCToothCharacter*> Crew;
    FFogWorld(int32 TeamSize=1,bool bReserve=true)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        Box(FVector(0,0,-10),FVector(2500,2500,10));
        if(bReserve) Tooth=Reserve(FVector(0,0,100),1);
        for(int32 I=0;I<TeamSize;++I)
        {
            const float Angle=I*2*PI/FMath::Max(1,TeamSize);
            auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(220*FMath::Cos(Angle),220*FMath::Sin(Angle),61),FRotator::ZeroRotator);
            auto* Controller=World->SpawnActor<APlayerController>(); Controller->Possess(Hero);
            Hero->SetActorTickEnabled(false); Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
            Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Hero->GetMesh()->SetComponentTickEnabled(false); Hero->ToothPhysics->SetComponentTickEnabled(false);
            // These rule fixtures keep recoil below their fall threshold. A separate case verifies the real 240 threshold wait.
            Hero->ToothPhysics->Settings.FallThreshold=1400;
            Hero->Grip->SetComponentTickEnabled(false); Crew.Add(Hero);
        }
        Event=World->SpawnActor<AMCFogBrawlEvent>();
    }
    ~FFogWorld()
    {
        World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    AActor* Box(FVector Center,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent); Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent(); Actor->SetActorLocation(Center); return Actor;
    }
    AMCArenaTooth* Reserve(FVector Center,int32 Id)
    {
        auto* Reserve=World->SpawnActor<AMCArenaTooth>(Center,FRotator::ZeroRotator);
        FMCArenaToothSettings Settings; Settings.InitialCalculusEveryNthTooth=0; Reserve->Initialize(Id,Settings);
        Reserve->SetActorTickEnabled(false); Reserve->Body->SetSimulatePhysics(false);
        Reserve->Body->SetBoxExtent(FVector(80,80,100)); return Reserve;
    }
    void Clock(double Time)
    {
        World->GetWorldSettings()->MaxUndilatedFrameTime=3600;
        ++GFrameCounter; World->Tick(LEVELTICK_TimeOnly,FMath::Max(.001f,float(Time-World->GetTimeSeconds())));
    }
    void Warning()
    {
        Event->Start(); Event->SetActorTickEnabled(false);
        Clock(Event->StageEndsAt+.01); Event->Tick(.01f); Event->SetActorTickEnabled(false);
    }
    void Impact()
    {
        Clock(Event->WarningEndsAt+.01); Event->Tick(.01f); Event->SetActorTickEnabled(false);
    }
    void NextWarning()
    {
        Clock(Event->StageEndsAt+.01); Event->Tick(.01f); Event->SetActorTickEnabled(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogSoloGuardTest,"MessControl.FogBrawl.GenericRMBProtectsReserveAndResolvesOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogSoloGuardTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F; F.Warning();
    if(!TestTrue(TEXT("A single controlled player can receive a strike warning"),F.Event->Stage==EMCFogBrawlStage::Warning && F.Event->ActiveTarget==F.Tooth)) return false;
    auto* Hero=F.Crew[0]; Hero->SetBraceInputHeld(true);
    TestTrue(TEXT("Holding generic RMB near the tooth counts without requiring a physical anchor"),F.Event->IsGuarding(Hero));
    const float Health=Hero->Status->State.Health, ReserveHealth=F.Tooth->State.Health;
    F.Clock(F.Event->WarningEndsAt-.1); F.Event->Tick(.01f);
    TestEqual(TEXT("The warning itself does no player damage"),Hero->Status->State.Health,Health);
    F.Impact();
    TestEqual(TEXT("A solo defender receives exactly fifteen damage"),Hero->Status->State.Health,Health-15);
    TestEqual(TEXT("A guarded tooth retains all its current health"),F.Tooth->State.Health,ReserveHealth);
    TestEqual(TEXT("A solo defender has the ordinary 280cm/s recoil"),F.Event->LastImpulse,280.f);
    TestFalse(TEXT("Physical brace intent is released before recoil"),Hero->Grip->IsBraceInputHeld());
    F.Event->Tick(.1f); F.Event->Tick(.1f);
    TestEqual(TEXT("Later recovery ticks cannot repeat the hit"),Hero->Status->State.Health,Health-15);
    TestEqual(TEXT("Exactly one strike was spent"),F.Event->StrikesResolved,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogSharedGuardTest,"MessControl.FogBrawl.TeamSharesLoadAndLoneDefenderTakesFullLoad",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogSharedGuardTest::RunTest(const FString&)
{
    {
        MCFogBrawlTestsPrivate::FFogWorld F(4); F.Warning();
        for(auto* Hero:F.Crew) Hero->SetBraceInputHeld(true);
        F.Impact();
        TestEqual(TEXT("Four participating defenders are counted on the server"),F.Event->LastGuardCount,4);
        TestEqual(TEXT("A full team gives each defender the solo damage"),F.Event->LastDamagePerGuard,15.f);
        TestEqual(TEXT("A full team gives each defender the ordinary recoil"),F.Event->LastImpulse,280.f);
        for(auto* Hero:F.Crew) TestEqual(TEXT("Every defender receives its share once"),Hero->Status->State.Health,85.f);
        TestTrue(TEXT("Team protection leaves the tooth available"),F.Tooth->IsAvailable());
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F(4); F.Warning();
        F.Crew[0]->SetBraceInputHeld(true); F.Crew[1]->SetBraceInputHeld(true); F.Impact();
        TestEqual(TEXT("Two guards split a four-player impact"),F.Event->LastGuardCount,2);
        TestEqual(TEXT("Each of the two guards takes thirty damage"),F.Event->LastDamagePerGuard,30.f);
        TestEqual(TEXT("Two guards halve the unbounded recoil"),F.Event->LastImpulse,560.f);
        TestEqual(TEXT("The first guard receives its half"),F.Crew[0]->Status->State.Health,70.f);
        TestEqual(TEXT("The second guard receives its half"),F.Crew[1]->Status->State.Health,70.f);
        TestEqual(TEXT("A nearby teammate without RMB takes no shared damage"),F.Crew[2]->Status->State.Health,100.f);
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F(4); F.Warning(); F.Crew[0]->SetBraceInputHeld(true); F.Impact();
        TestEqual(TEXT("One guard carries the four-player damage budget"),F.Event->LastDamagePerGuard,60.f);
        TestEqual(TEXT("One guard's recoil is capped at 1100cm/s"),F.Event->LastImpulse,1100.f);
        TestEqual(TEXT("The lone guard takes that full share"),F.Crew[0]->Status->State.Health,40.f);
        TestTrue(TEXT("Even one defender saves the tooth"),F.Tooth->IsAvailable());
        TestEqual(TEXT("Unbraced teammates take no strike damage"),F.Crew[1]->Status->State.Health,100.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogMissAndRetargetTest,"MessControl.FogBrawl.MissUsesNormalToothLossAndConsumedTargetRestartsWarning",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogMissAndRetargetTest::RunTest(const FString&)
{
    {
        MCFogBrawlTestsPrivate::FFogWorld F; F.Warning(); F.Impact();
        TestTrue(TEXT("No defenders lose the actual existing reserve tooth"),F.Tooth->State.bLost && F.Tooth->State.Health==0);
        TestFalse(TEXT("The normal reserve availability rule observes the loss"),F.Tooth->IsAvailable());
        TestEqual(TEXT("A failed defence still resolves exactly one strike"),F.Event->StrikesResolved,1);
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F; auto* Second=F.Reserve(FVector(0,300,100),2); F.Warning();
        auto* Consumed=F.Event->ActiveTarget.Get();
        if(!TestNotNull(TEXT("The fixture obtains a real reserve target"),Consumed)) return false;
        auto* Remaining=Consumed==F.Tooth?Second:F.Tooth;
        Consumed->ConsumeForRespawn(); const double OldDeadline=F.Event->WarningEndsAt;
        F.Clock(OldDeadline-.1); F.Event->Tick(.01f);
        TestTrue(TEXT("A consumed reserve is replaced by an available target"),F.Event->ActiveTarget==Remaining && Remaining->IsAvailable());
        TestTrue(TEXT("The replacement gets a full new warning window"),F.Event->WarningEndsAt>OldDeadline+4);
        TestEqual(TEXT("Consumption does not spend an event strike"),F.Event->StrikesResolved,0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogGuardGeometryTest,"MessControl.FogBrawl.DefenceRequiresNearGroundedLivingCrewAndClearApproach",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogGuardGeometryTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F; F.Warning(); auto* Hero=F.Crew[0]; Hero->SetBraceInputHeld(true);
    if(!TestTrue(TEXT("The initial nearby grounded defender qualifies"),F.Event->IsGuarding(Hero))) return false;
    auto* Wall=F.Box(FVector(140,0,100),FVector(15,150,100));
    TestFalse(TEXT("A wall between the player and tooth prevents defending through it"),F.Event->IsGuarding(Hero));
    TestFalse(TEXT("A wall also hides the target marker"),F.Event->ShouldRevealMarker(Hero));
    Wall->Destroy(); Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    TestFalse(TEXT("An airborne defender cannot absorb the strike"),F.Event->IsGuarding(Hero));
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Hero->SetActorLocation(FVector(650,0,61),false,nullptr,ETeleportType::TeleportPhysics);
    TestFalse(TEXT("RMB beyond the body's guard radius cannot protect it"),F.Event->IsGuarding(Hero));
    TestTrue(TEXT("A nearby hero can discover the tell before reaching guard range"),F.Event->ShouldRevealMarker(Hero));
    Hero->SetActorLocation(FVector(1200,0,61),false,nullptr,ETeleportType::TeleportPhysics);
    TestFalse(TEXT("A distant hero cannot see the marker through the global fog"),F.Event->ShouldRevealMarker(Hero));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogLifecycleTest,"MessControl.FogBrawl.CompleteCancelReplaceAndMissingReserve",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogLifecycleTest::RunTest(const FString&)
{
    {
        MCFogBrawlTestsPrivate::FFogWorld F; F.Warning();
        for(int32 I=0;I<F.Event->TotalStrikes;++I)
        {
            F.Crew[0]->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            F.Crew[0]->SetBraceInputHeld(true); F.Impact();
            // Physics ticks are disabled in this rules fixture. Explicitly finish
            // the get-up before advancing; the recovery-wait case tests the delay.
            F.Crew[0]->ToothPhysics->SetThroatCaptured(false);
            F.Crew[0]->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            F.NextWarning();
        }
        TestTrue(TEXT("All five actual defences complete the event"),F.Event->IsComplete());
        TestEqual(TEXT("Completion preserves the final strike count"),F.Event->StrikesResolved,5);
        TestNull(TEXT("Completion clears the active target"),F.Event->ActiveTarget.Get());
        TestFalse(TEXT("Completion stops gameplay tick"),F.Event->IsActorTickEnabled());
        F.Event->Start(); auto* Old=F.Event;
        F.Event=F.World->SpawnActor<AMCFogBrawlEvent>(); F.Event->Start();
        TestTrue(TEXT("Starting another fog event replaces the previous owner"),Old->IsActorBeingDestroyed());
        F.Event->Stop();
        TestFalse(TEXT("Cancellation cannot be mistaken for successful completion"),F.Event->IsComplete());
        TestFalse(TEXT("Cancellation stops active rules"),F.Event->IsActive());
        TestEqual(TEXT("Cancellation clears all pulse geometry"),F.Event->Pulse->GetNumSections(),0);
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F(1,false); F.Warning();
        TestTrue(TEXT("A missing reachable reserve reports failed setup"),F.Event->bFailed);
        TestFalse(TEXT("A failed setup cannot report victory"),F.Event->IsComplete());
        TestFalse(TEXT("A failed setup leaves no active strike rules"),F.Event->IsActive());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogAuthorityTest,"MessControl.FogBrawl.ClientCannotStartOrResolveStrikes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogAuthorityTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F; F.Event->SetRole(ROLE_SimulatedProxy); F.Event->Start();
    TestEqual(TEXT("A client cannot start an authoritative event"),F.Event->Stage,EMCFogBrawlStage::Idle);
    F.Event->Stage=EMCFogBrawlStage::Warning; F.Event->ActiveTarget=F.Tooth; F.Event->WarningEndsAt=0;
    F.Crew[0]->SetBraceInputHeld(true); F.Event->Tick(5);
    TestEqual(TEXT("Client presentation cannot hurt the defender"),F.Crew[0]->Status->State.Health,100.f);
    TestEqual(TEXT("Client presentation cannot hurt the reserve"),F.Tooth->State.Health,100.f);
    TestEqual(TEXT("Client presentation cannot advance strike count"),F.Event->StrikesResolved,0);
    F.Event->Stop(); TestEqual(TEXT("A client cannot cancel replicated server state"),F.Event->Stage,EMCFogBrawlStage::Warning);
    F.Event->SetRole(ROLE_Authority); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogRecoveryWaitTest,"MessControl.FogBrawl.WaitsForNormalGetupAndStopsAfterShiftTerminal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogRecoveryWaitTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F; F.Warning(); auto* Hero=F.Crew[0];
    Hero->ToothPhysics->Settings.FallThreshold=240; Hero->SetBraceInputHeld(true); F.Impact();
    if(!TestFalse(TEXT("The ordinary 280 recoil enters the existing ragdoll state"),Hero->CanWork())) return false;
    F.NextWarning();
    TestFalse(TEXT("A crew still recovering does not fail the event"),F.Event->bFailed);
    TestTrue(TEXT("The next warning waits without spending another strike"),F.Event->Stage==EMCFogBrawlStage::Recovery && F.Event->StrikesResolved==1);
    Hero->ToothPhysics->SetThroatCaptured(false); Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    F.NextWarning();
    TestEqual(TEXT("A capable crew receives a full fresh warning after recovery"),F.Event->Stage,EMCFogBrawlStage::Warning);
    auto* State=F.World->SpawnActor<AMCGameState>(); F.World->SetGameState(State); State->Phase=EMCShiftPhase::Lost;
    F.Event->Tick(.01f);
    TestFalse(TEXT("Standalone fog stops when the shift ends"),F.Event->IsActive());
    TestEqual(TEXT("A terminal shift clears the local pulse"),F.Event->Pulse->GetNumSections(),0);
    return true;
}
#endif
