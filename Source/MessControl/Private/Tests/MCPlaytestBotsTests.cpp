#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCBrushContactComponent.h"
#include "MCGripComponent.h"
#include "MCInventoryComponent.h"
#include "MCPlaytestBotController.h"
#include "MCPlaytestSession.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"

namespace MCPlaytestBotsTestsPrivate
{
    struct FWorldFixture
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        AMCGameMode* Mode=nullptr;
        AMCGameState* State=nullptr;

        FWorldFixture()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;
            URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
            URL.AddOption(TEXT("Seed=41"));
            World->SetGameMode(URL);
            World->InitializeActorsForPlay(URL);
            World->BeginPlay();
            Mode=World->GetAuthGameMode<AMCGameMode>();
            State=World->GetGameState<AMCGameState>();
            // Exercise the production shift reset/respawn, with fixed native pawn
            // and isolated spawn geometry rather than map-dependent collision.
            Mode->DefaultPawnClass=AMCToothCharacter::StaticClass();
            Mode->bUseDayOnePlan=false;
            Mode->SetActorTickEnabled(false);
            State->RunSettings.MaxPlayers=4;

            auto* Floor=World->SpawnActor<AActor>();
            auto* Collision=NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Collision);
            Collision->SetBoxExtent(FVector(2200,2200,20));
            Collision->SetCollisionProfileName(TEXT("BlockAll"));
            Collision->RegisterComponent();
            Floor->SetActorLocation(FVector(0,5000,-20));
            for(int32 Index=0;Index<4;++Index)
                World->SpawnActor<APlayerStart>(FVector(Index*320-480,5000,130),FRotator::ZeroRotator);
        }

        ~FWorldFixture()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        APlayerController* AddHuman()
        {
            auto* Controller=World->SpawnActor<APlayerController>();
            Mode->RestartPlayer(Controller);
            return Controller;
        }

        TArray<AMCPlaytestBotController*> Bots() const
        {
            TArray<AMCPlaytestBotController*> Result;
            for(TActorIterator<AMCPlaytestBotController> It(World);It;++It)
                if(IsValid(*It) && !It->IsActorBeingDestroyed()) Result.Add(*It);
            return Result;
        }

        void Step(float Seconds)
        {
            for(int32 Index=0;Index<FMath::CeilToInt(Seconds*60);++Index)
            {
                ++GFrameCounter;
                World->Tick(LEVELTICK_All,1.f/60);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestCoopRosterTest,"MessControl.PlaytestBots.CoopCapacityAndIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestCoopRosterTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Human=Fixture.AddHuman();
    auto* Session=Fixture.World->SpawnActor<AMCPlaytestSession>();
    if(!TestNotNull(TEXT("Human uses the normal player pawn"),Cast<AMCToothCharacter>(Human->GetPawn()))
        || !TestNotNull(TEXT("Playtest session was created"),Session)) return false;
    FString Error;
    const int32 OriginalSeed=Fixture.State->RunSeed;
    TestFalse(TEXT("A human plus four bots exceeds the team limit"),
        Session->StartSession(4,EMCPlaytestBotSkill::Regular,false,123,Error));
    TestFalse(TEXT("Rejected setup stays inactive"),Session->IsActive());
    TestEqual(TEXT("Rejected setup spawns no bots"),Fixture.Bots().Num(),0);
    TestEqual(TEXT("Rejected setup does not reset the current shift"),Fixture.State->RunSeed,OriginalSeed);
    if(!TestTrue(TEXT("A human plus three bots starts a full team"),
        Session->StartSession(3,EMCPlaytestBotSkill::Regular,false,123,Error)))
    { AddError(Error); return false; }
    TestEqual(TEXT("Requested seed starts the normal game run"),Fixture.State->RunSeed,123);
    TestFalse(TEXT("The coop human stays playable"),Human->PlayerState->IsOnlyASpectator());
    TestNotNull(TEXT("The coop human has a normal pawn after reset"),Cast<AMCToothCharacter>(Human->GetPawn()));
    const auto Bots=Fixture.Bots();
    TestEqual(TEXT("Exactly three AI controllers exist"),Bots.Num(),3);
    TSet<AMCPlayerState*> Identities;
    for(auto* Bot:Bots)
    {
        auto* Pawn=Cast<AMCToothCharacter>(Bot->GetPawn());
        auto* Identity=Bot->GetPlayerState<AMCPlayerState>();
        if(!TestNotNull(TEXT("Each bot possesses the normal character"),Pawn)
            || !TestNotNull(TEXT("Each bot has the normal player state"),Identity)) return false;
        Identities.Add(Identity);
        TestTrue(TEXT("Bot identity is in the replicated player roster"),Fixture.State->PlayerArray.Contains(Identity));
        TestTrue(TEXT("AI identity is marked as a bot"),Identity->IsABot());
        TestFalse(TEXT("Bot identity is a gameplay participant"),Identity->IsOnlyASpectator());
        TestFalse(TEXT("Starting a bot grants no waterjet upgrade"),Pawn->Inventory->bWaterJetUnlocked);
        TestEqual(TEXT("Starting a bot grants no points"),Identity->Points,0);
        TestEqual(TEXT("Normal maximum health is preserved"),Pawn->Status->State.Health,Pawn->Status->State.MaxHealth);
    }
    TestEqual(TEXT("Each teammate has its own persistent identity"),Identities.Num(),3);
    TestFalse(TEXT("Active sessions reject a second start"),
        Session->StartSession(1,EMCPlaytestBotSkill::Skilled,false,456,Error));
    TestEqual(TEXT("Rejected second start preserves the current bot roster"),Fixture.Bots().Num(),3);
    TestEqual(TEXT("Rejected second start preserves the current seed"),Fixture.State->RunSeed,123);
    Session->StopSession();
    TestFalse(TEXT("Stopping deactivates the session"),Session->IsActive());
    TestEqual(TEXT("Stopping destroys all bot controllers"),Fixture.Bots().Num(),0);
    for(auto* Identity:Identities)
        TestFalse(TEXT("Stopping removes AI identities from the replicated roster"),Fixture.State->PlayerArray.Contains(Identity));
    TestNotNull(TEXT("The human can play after stop"),Cast<AMCToothCharacter>(Human->GetPawn()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestObserverTest,"MessControl.PlaytestBots.ObserverTeamAndShiftOutcome",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestObserverTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Human=Fixture.AddHuman();
    auto* Session=Fixture.World->SpawnActor<AMCPlaytestSession>();
    if(!TestNotNull(TEXT("Observer identity exists"),Human->PlayerState.Get())
        || !TestNotNull(TEXT("Playtest session exists"),Session)) return false;
    const bool WasOnlySpectator=Human->PlayerState->IsOnlyASpectator();
    const bool WasSpectator=Human->PlayerState->IsSpectator();
    FString Error;
    if(!TestTrue(TEXT("Four bots plus an observer fit in four gameplay places"),
        Session->StartSession(4,EMCPlaytestBotSkill::Regular,true,41,Error)))
    { AddError(Error); return false; }
    TestTrue(TEXT("Session records the human observer"),Session->IsObserver(Human));
    TestTrue(TEXT("Observer identity is excluded from gameplay"),Human->PlayerState->IsOnlyASpectator());
    TestFalse(TEXT("Observer has no controllable worker pawn"),IsValid(Cast<AMCToothCharacter>(Human->GetPawn())));
    TestEqual(TEXT("Observer mode creates four ordinary AI participants"),Fixture.Bots().Num(),4);

    // A living AI team must be able to win with no human pawn and no reserve
    // teeth. PlayerController-only counting incorrectly ends this as a loss.
    Fixture.State->ArenaTeeth.Empty();
    Fixture.State->Phase=EMCShiftPhase::Intermission;
    Fixture.State->Day=Fixture.State->RunSettings.DaysToSurvive;
    Fixture.State->MouthHealth=Fixture.State->RunSettings.MaxMouthHealth;
    Fixture.State->bDayOneComplete=false;
    Fixture.Mode->Tick(.01f);
    TestEqual(TEXT("A living autonomous team can complete the shift"),Fixture.State->Phase,EMCShiftPhase::Won);

    Fixture.State->Phase=EMCShiftPhase::Intermission;
    for(auto* Bot:Fixture.Bots())
        if(auto* Pawn=Cast<AMCToothCharacter>(Bot->GetPawn())) Pawn->Status->Damage(Pawn->Status->State.MaxHealth);
    Fixture.Mode->Tick(.01f);
    TestEqual(TEXT("An observer cannot prevent a dead team without reserves from losing"),Fixture.State->Phase,EMCShiftPhase::Lost);

    Session->StopSession();
    TestFalse(TEXT("Stopping removes observer registration"),Session->IsObserver(Human));
    TestEqual(TEXT("Stopping restores the original spectator-only flag"),Human->PlayerState->IsOnlyASpectator(),WasOnlySpectator);
    TestEqual(TEXT("Stopping restores the original spectator flag"),Human->PlayerState->IsSpectator(),WasSpectator);
    TestNotNull(TEXT("Stopping returns the human to a fresh gameplay pawn"),Cast<AMCToothCharacter>(Human->GetPawn()));
    TestEqual(TEXT("Stopping leaves no bot controllers"),Fixture.Bots().Num(),0);
    Session->StopSession();
    TestNotNull(TEXT("Stopping twice preserves the human pawn"),Cast<AMCToothCharacter>(Human->GetPawn()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestActionEligibilityTest,"MessControl.PlaytestBots.IneligiblePawnReleasesInputs",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestActionEligibilityTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Bot=Fixture.World->SpawnActor<AMCPlaytestBotController>();
    auto* Pawn=Fixture.World->SpawnActor<AMCToothCharacter>(FVector(0,5000,130),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("Bot controller exists"),Bot) || !TestNotNull(TEXT("Ordinary worker pawn exists"),Pawn)) return false;
    Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);
    Bot->Possess(Pawn);
    auto* Move=Cast<UMCToothMovementComponent>(Pawn->GetCharacterMovement());
    if(!TestNotNull(TEXT("Bot uses the production stamina movement component"),Move)) return false;
    Fixture.State->Phase=EMCShiftPhase::Working;
    Pawn->ServerSetPrimary(true); // Swimming/cling bypasses the work-input edge flag.
    TestTrue(TEXT("Ordinary RPC can hold the swimming/cling action"),Pawn->IsPrimaryHeld());
    Pawn->SetPrimaryInputHeld(false);
    TestFalse(TEXT("Shared release clears primary set through swimming/cling RPC"),Pawn->IsPrimaryHeld());
    Pawn->SetPrimaryInputHeld(true);
    Pawn->SetSprintInputHeld(true);
    Pawn->SetJumpInputHeld(true);
    Pawn->AddMovementInput(FVector::ForwardVector,1);
    Move->SetWantsDash(true);
    const float Health=Pawn->Status->State.Health;
    const float Stamina=Pawn->GetStamina();

    Fixture.State->Phase=EMCShiftPhase::Won;
    Bot->Tick(.1f);
    TestFalse(TEXT("Match completion makes the bot release primary input"),Pawn->IsPrimaryHeld());
    TestFalse(TEXT("Match completion makes the bot release sprint"),Move->WantsToSprint());
    TestFalse(TEXT("Match completion makes the bot release dash"),Move->WantsDash());
    TestFalse(TEXT("Match completion makes the bot release jump"),Pawn->bPressedJump);
    TestTrue(TEXT("Match completion discards pending locomotion input"),Pawn->GetPendingMovementInputVector().IsNearlyZero());
    TestEqual(TEXT("The controller cannot heal an ineligible worker"),Pawn->Status->State.Health,Health);
    TestEqual(TEXT("The controller cannot refill stamina"),Pawn->GetStamina(),Stamina);

    Fixture.State->Phase=EMCShiftPhase::Working;
    Pawn->SetPrimaryInputHeld(true);
    Pawn->Status->Damage(Pawn->Status->State.MaxHealth);
    Bot->Tick(.1f);
    TestFalse(TEXT("Dead bots release primary input while awaiting normal respawn"),Pawn->IsPrimaryHeld());
    TestEqual(TEXT("A bot cannot revive itself by ticking its controller"),Pawn->Status->State.Health,0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestOrdinaryCareTest,"MessControl.PlaytestBots.SelfRepairUsesTimedContact",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestOrdinaryCareTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Bot=Fixture.World->SpawnActor<AMCPlaytestBotController>();
    auto* Pawn=Fixture.World->SpawnActor<AMCToothCharacter>(FVector(0,5000,130),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("Bot exists"),Bot) || !TestNotNull(TEXT("Ordinary worker exists"),Pawn)) return false;
    Pawn->GetCharacterMovement()->DisableMovement();
    Fixture.State->Phase=EMCShiftPhase::Working;
    Pawn->Status->Settings.ContactSeconds=.5f;
    Pawn->Status->Settings.HealPerContact=25.f;
    Pawn->Status->Settings.bAllowSelfCare=true;
    Pawn->Status->Damage(Pawn->Status->State.MaxHealth*.35f);
    const float DamagedHealth=Pawn->Status->State.Health;
    Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);
    Bot->Possess(Pawn);

    // Run the real timer, actor ticks and normal care contact. A decision by
    // itself must never create healing before the configured contact duration.
    Fixture.Step(.2f);
    TestEqual(TEXT("Choosing self repair cannot manufacture early healing"),Pawn->Status->State.Health,DamagedHealth);
    Fixture.Step(2.f);
    TestTrue(TEXT("Bot-selected self repair makes real timed contact progress"),Pawn->Status->State.Health>DamagedHealth);
    TestTrue(TEXT("Self repair cannot exceed ordinary maximum health"),Pawn->Status->State.Health<=Pawn->Status->State.MaxHealth);
    TestFalse(TEXT("Ordinary care grants no tool upgrade"),Pawn->Inventory->bWaterJetUnlocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestApproachClearanceTest,"MessControl.PlaytestBots.ApproachRejectsTeethAndPlayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestApproachClearanceTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Bot=Fixture.World->SpawnActor<AMCPlaytestBotController>();
    auto* Hero=Fixture.World->SpawnActor<AMCToothCharacter>(FVector(-400,5000,100),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("Approach controller exists"),Bot) || !TestNotNull(TEXT("Approach uses the normal pawn"),Hero)) return false;
    Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);
    Bot->Possess(Hero);
    const float StandingZ=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4;
    const FVector FreeFloor(-650,5000,StandingZ);
    const FTransform ToothTransform(FVector(0,5000,100));
    auto* Tooth=Fixture.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),ToothTransform);
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("Collision fixture uses a real tooth"),Tooth) || !TestNotNull(TEXT("Collision fixture mesh exists"),Mesh)) return false;
    Tooth->SetAppearance(Mesh,FVector(2));
    Tooth->FinishSpawning(ToothTransform);
    auto* Other=Fixture.World->SpawnActor<AMCToothCharacter>(FVector(400,5000,StandingZ),FRotator::ZeroRotator);
    if(!TestNotNull(TEXT("A teammate uses an ordinary blocking capsule"),Other)) return false;

    TestTrue(TEXT("Standing over clear floor is a valid work position"),Bot->IsApproachPositionClear(FreeFloor));
    TestTrue(TEXT("The worker ignores its own capsule when checking a work position"),Bot->IsApproachPositionClear(Hero->GetActorLocation()));
    TestFalse(TEXT("A work position inside the physical crown is rejected"),Bot->IsApproachPositionClear(Tooth->GetActorLocation()));
    TestFalse(TEXT("A work position inside another player's capsule is rejected"),Bot->IsApproachPositionClear(Other->GetActorLocation()));
    const float Radius=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
    TestFalse(TEXT("Capsule clearance rejects a position whose centre is outside but whose body clips the crown"),
        Bot->IsApproachPositionClear(FVector(Tooth->Body->GetScaledBoxExtent().X+Radius*.5f,5000,StandingZ)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestUnreachableGrimeTest,"MessControl.PlaytestBots.UnreachableGrimeDoesNotGrabTeammate",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestUnreachableGrimeTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Bot=Fixture.World->SpawnActor<AMCPlaytestBotController>();
    auto* Hero=Fixture.World->SpawnActor<AMCToothCharacter>(FVector(0,5000,130),FRotator::ZeroRotator);
    auto* Human=Fixture.World->SpawnActor<APlayerController>();
    auto* Teammate=Fixture.World->SpawnActor<AMCToothCharacter>(FVector(100,5000,130),FRotator::ZeroRotator);
    const FTransform ToothTransform(FVector(75,4900,100));
    auto* Tooth=Fixture.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),ToothTransform);
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("Worker exists"),Hero) || !TestNotNull(TEXT("Bot exists"),Bot)
        || !TestNotNull(TEXT("Human controller exists"),Human) || !TestNotNull(TEXT("Clean teammate exists"),Teammate)
        || !TestNotNull(TEXT("Dirty crown exists"),Tooth) || !TestNotNull(TEXT("Crown mesh exists"),Mesh)) return false;
    Tooth->SetAppearance(Mesh,FVector(1));
    Tooth->FinishSpawning(ToothTransform);
    Tooth->SetCoffee(1);
    Hero->GetCharacterMovement()->DisableMovement();
    Teammate->GetCharacterMovement()->DisableMovement();
    Human->Possess(Teammate);
    Fixture.State->Phase=EMCShiftPhase::Working;
    Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);
    Bot->Possess(Hero);
    const int32 InitialLayers=Tooth->Status->State.CoffeeLeft;
    FVector Contact,Normal;
    TArray<FVector> DirtyPoints,DirtyNormals;
    Tooth->GetDirtyContactSamples(DirtyPoints,DirtyNormals);
    TestTrue(TEXT("The nearby crown contains actual dirty surface samples"),InitialLayers>0 && !DirtyPoints.IsEmpty());
    TestFalse(TEXT("The ordinary brush cannot reach grime on the opposite face from this position"),Tooth->FindDirtyContact(Hero,Contact,Normal));
    TestTrue(TEXT("The bystander is within the ordinary accidental grab range"),
        FVector::Dist(Hero->GetActorLocation(),Teammate->GetActorLocation())<130);
    bool bGrabbedTeammate=false,bHeldIneffectivePrimary=false;
    // Real bot decisions and ordinary pawn action resolution. The crown's
    // bounds are close, but the remaining coating is on its opposite face.
    // Pressing primary from here would grab the clean bystander instead.
    for(int32 Index=0;Index<120;++Index)
    {
        Fixture.Step(1.f/60);
        bGrabbedTeammate|=Hero->Grip->GrabbedPlayer==Teammate;
        bHeldIneffectivePrimary|=Hero->IsPrimaryHeld();
    }
    TestFalse(TEXT("Unreachable cleaning cannot fall through into a teammate grab"),bGrabbedTeammate);
    TestFalse(TEXT("A bot cannot hold cleaning input without an available contact"),bHeldIneffectivePrimary);
    TestEqual(TEXT("An unreachable target retains its ordinary dirty layers"),Tooth->Status->State.CoffeeLeft,InitialLayers);
    TestEqual(TEXT("No brush contact is manufactured for the unreachable crown"),Hero->SuccessfulBrushContacts,0);
    return true;
}
#endif
