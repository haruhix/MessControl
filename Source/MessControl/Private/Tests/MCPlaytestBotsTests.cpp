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
#include "MCPerkComponent.h"
#include "MCProgressionComponent.h"
#include "MCSingleDayDirector.h"
#include "MCTutorialDirector.h"
#include "MCNutRainEvent.h"
#include "MCTongue.h"
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
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

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
            Mode=World->GetAuthGameMode<AMCGameMode>();
            Mode->bUseDayOnePlan=false;
            Mode->bUseAdaptiveDirector=false;
            World->InitializeActorsForPlay(URL);
            World->BeginPlay();
            State=World->GetGameState<AMCGameState>();
            // Exercise the production shift reset/respawn, with fixed native pawn
            // and isolated spawn geometry rather than map-dependent collision.
            Mode->DefaultPawnClass=AMCToothCharacter::StaticClass();
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

        bool StartSingleDayTraining()
        {
            auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
            if(!Cube) return false;
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
            const FTransform Pose(FRotator::ZeroRotator,FVector(0,5000,-10),FVector(24,16,.2));
            auto* Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose,
                nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Tongue) return false;
            Tongue->SourceMesh=Cube;
            Tongue->bAutomaticYawns=false;
            Tongue->FinishSpawning(Pose);
            Tongue->SetActorTickEnabled(false);
            Mode->bUseSingleDayLoop=true;
            Mode->RestartShift();
            // Start the normal lesson, then let StartSession perform its own
            // diagnostic reset. No tutorial-completed flags are manufactured.
            Mode->Tick(.01f);
            return IsValid(Mode->TutorialDirector) && IsValid(State->SingleDayDirector)
                && State->bTutorialActive;
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

    int32 PerkStacks(const AMCPlayerState* Player)
    {
        int32 Total=0;
        if(Player && Player->Perks)
            for(const auto& Perk:Player->Perks->ActivePerks) Total+=Perk.Stacks;
        return Total;
    }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestSingleDayObserveTest,"MessControl.PlaytestBots.SingleDayObserveLaunchAndPersonalOffers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestSingleDayObserveTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Human=Fixture.AddHuman();
    auto* Observer=Human->GetPlayerState<AMCPlayerState>();
    auto* Session=Fixture.World->SpawnActor<AMCPlaytestSession>();
    if(!TestNotNull(TEXT("Normal human identity exists"),Observer)
        || !TestNotNull(TEXT("Playtest session exists"),Session)
        || !TestTrue(TEXT("An ordinary restart starts real single-day training"),Fixture.StartSingleDayTraining())) return false;
    TestEqual(TEXT("Normal single-day launch starts in training"),Fixture.State->SingleDayDirector->Stage,EMCSingleDayStage::Training);
    FString Error;
    if(!TestTrue(TEXT("Observe can launch four actual AI participants from training"),
        Session->StartSession(4,EMCPlaytestBotSkill::Skilled,true,41,Error)))
    { AddError(Error); return false; }
    TestFalse(TEXT("Diagnostic reset releases tutorial safety"),Fixture.State->bTutorialActive);
    TestNull(TEXT("Diagnostic reset stops the old lesson director"),Fixture.Mode->TutorialDirector.Get());
    TestTrue(TEXT("The human is registered as an observer"),Session->IsObserver(Human));
    TestTrue(TEXT("Observer identity is excluded before initial XP"),Observer->IsOnlyASpectator());
    TestEqual(TEXT("Observer receives no personal level choice"),Observer->PendingLevelChoices,0);
    TestEqual(TEXT("Observer receives no perk"),MCPlaytestBotsTestsPrivate::PerkStacks(Observer),0);
    TestEqual(TEXT("Initial experience creates exactly the first team level"),Fixture.State->Progression->TeamLevel,2);
    TestEqual(TEXT("Initial XP is awarded once for the complete team"),Fixture.State->Progression->TotalExperience,
        int64(Fixture.State->Progression->FirstLevelExperience));
    const auto Bots=Fixture.Bots();
    if(!TestEqual(TEXT("The session has four real AI controllers"),Bots.Num(),4)) return false;
    TSet<AMCPlayerState*> Identities;
    for(auto* Bot:Bots)
    {
        auto* Player=Bot->GetPlayerState<AMCPlayerState>();
        if(!TestNotNull(TEXT("Each AI owns a normal worker pawn"),Cast<AMCToothCharacter>(Bot->GetPawn()))
            || !TestNotNull(TEXT("Each AI owns a persistent player state"),Player)) return false;
        Identities.Add(Player);
        TestTrue(TEXT("Every AI participates in the replicated roster"),Fixture.State->PlayerArray.Contains(Player));
        TestTrue(TEXT("Every AI is identified as a bot"),Player->IsABot());
        TestEqual(TEXT("Each AI claims its initial personal choice"),Player->PendingLevelChoices,0);
        TestFalse(TEXT("Claimed AI card payload is cleared"),Player->LevelUpOffer.IsValid());
        TestEqual(TEXT("Each AI owns exactly one initial perk stack"),MCPlaytestBotsTestsPrivate::PerkStacks(Player),1);
    }
    TestEqual(TEXT("All four AI identities remain distinct"),Identities.Num(),4);
    TestEqual(TEXT("Personal bot choices create no shared tool mask"),Fixture.State->TeamToolUpgrades,uint8(0));
    TestFalse(TEXT("No bot or observer blocks the first-card gate"),Fixture.State->Progression->HasPendingChoices());
    Fixture.Step(.35f);
    auto* Loop=Fixture.State->SingleDayDirector.Get();
    TestEqual(TEXT("Actual director ticks advance the autonomous team into nuts"),Loop->Stage,EMCSingleDayStage::Nuts);
    if(!TestNotNull(TEXT("Autonomous launch creates the real nut event"),Loop->NutEvent.Get())) return false;
    TestFalse(TEXT("The event finds a live tongue and usable nut collision"),Loop->NutEvent->bFailed);
    TestEqual(TEXT("The event enters real rainfall"),Loop->NutEvent->Stage,EMCNutRainStage::Rainfall);
    TestTrue(TEXT("Rainfall has actually launched food"),Loop->NutEvent->NutsSpawned>0);

    Session->StopSession();
    TestFalse(TEXT("Stop ends the diagnostic session"),Session->IsActive());
    TestEqual(TEXT("Stop removes every AI controller"),Fixture.Bots().Num(),0);
    for(auto* Player:Identities)
        TestFalse(TEXT("Stop removes each AI player state from the roster"),Fixture.State->PlayerArray.Contains(Player));
    TestFalse(TEXT("Stop restores the human participant flag"),Observer->IsOnlyASpectator());
    TestFalse(TEXT("Stop removes the human observer registration"),Session->IsObserver(Human));
    TestNotNull(TEXT("Stop restores a normal human worker"),Cast<AMCToothCharacter>(Human->GetPawn()));
    TestTrue(TEXT("Stop restores ordinary tutorial safety"),Fixture.State->bTutorialActive);
    TestEqual(TEXT("Stop starts a fresh training sequence"),Fixture.State->SingleDayDirector->Stage,EMCSingleDayStage::Training);
    TestNull(TEXT("Stop leaves no previous nut event"),Fixture.State->SingleDayDirector->NutEvent.Get());
    TestEqual(TEXT("Stop resets the team level"),Fixture.State->Progression->TeamLevel,1);
    TestEqual(TEXT("Stop resets shared XP"),Fixture.State->Progression->TotalExperience,int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestSingleDayCoopTest,"MessControl.PlaytestBots.SingleDayCoopPersonalGateAndQueuedChoices",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestSingleDayCoopTest::RunTest(const FString&)
{
    MCPlaytestBotsTestsPrivate::FWorldFixture Fixture;
    auto* Human=Fixture.AddHuman();
    auto* Player=Human->GetPlayerState<AMCPlayerState>();
    auto* Session=Fixture.World->SpawnActor<AMCPlaytestSession>();
    if(!TestNotNull(TEXT("Coop human identity exists"),Player)
        || !TestNotNull(TEXT("Coop session exists"),Session)
        || !TestTrue(TEXT("Coop starts from the ordinary single-day lesson"),Fixture.StartSingleDayTraining())) return false;
    FString Error;
    if(!TestTrue(TEXT("A human and bot can start coop while the new lesson is active"),
        Session->StartSession(1,EMCPlaytestBotSkill::Regular,false,41,Error)))
    { AddError(Error); return false; }
    const auto Bots=Fixture.Bots();
    if(!TestEqual(TEXT("Coop creates exactly one real AI controller"),Bots.Num(),1)) return false;
    auto* BotPlayer=Bots[0]->GetPlayerState<AMCPlayerState>();
    if(!TestNotNull(TEXT("Coop AI owns its own player state"),BotPlayer)
        || !TestNotNull(TEXT("Coop AI possesses the production pawn"),Cast<AMCToothCharacter>(Bots[0]->GetPawn()))) return false;
    TestFalse(TEXT("Coop human remains a gameplay participant"),Player->IsOnlyASpectator());
    TestFalse(TEXT("Coop human is never registered as an observer"),Session->IsObserver(Human));
    TestFalse(TEXT("Diagnostic coop reset releases tutorial safety"),Fixture.State->bTutorialActive);
    TestEqual(TEXT("Coop initial XP earns the shared first level"),Fixture.State->Progression->TeamLevel,2);
    if(!TestTrue(TEXT("Human gets an independent three-card offer"),Player->LevelUpOffer.IsValid())) return false;
    TestEqual(TEXT("Human still has one unclaimed choice"),Player->PendingLevelChoices,1);
    TestEqual(TEXT("The actual AI claims its own first card"),BotPlayer->PendingLevelChoices,0);
    TestEqual(TEXT("Bot grant belongs to the bot"),MCPlaytestBotsTestsPrivate::PerkStacks(BotPlayer),1);
    TestEqual(TEXT("Bot selection grants nothing to the human"),MCPlaytestBotsTestsPrivate::PerkStacks(Player),0);
    const FGuid FirstHumanOffer=Player->LevelUpOffer.OfferId;
    const TArray<FName> FirstHumanCards=Player->LevelUpOffer.PerkIDs;
    Fixture.Step(.35f);
    TestEqual(TEXT("The pending human card holds the real first-perk stage"),Fixture.State->SingleDayDirector->Stage,EMCSingleDayStage::FirstPerk);
    TestNull(TEXT("No rain launches before the human chooses"),Fixture.State->SingleDayDirector->NutEvent.Get());

    auto* Progression=Fixture.State->Progression.Get();
    const int32 NextThreshold=Progression->GetExperienceToNextLevel();
    TestEqual(TEXT("A shared award can earn two additional levels at once"),
        Progression->AddExperience(NextThreshold*2+Progression->ExperienceGrowthPerLevel),2);
    TestEqual(TEXT("Human retains all three independently queued choices"),Player->PendingLevelChoices,3);
    TestTrue(TEXT("More XP preserves the human's current offer token"),Player->LevelUpOffer.OfferId==FirstHumanOffer);
    TestTrue(TEXT("More XP preserves the human's current three cards"),Player->LevelUpOffer.PerkIDs==FirstHumanCards);
    TestEqual(TEXT("Immediate bot synchronization consumes only one queued level"),BotPlayer->PendingLevelChoices,1);
    Fixture.Step(.6f);
    TestEqual(TEXT("Real progression timer drains the bot's remaining queued level"),BotPlayer->PendingLevelChoices,0);
    TestFalse(TEXT("Bot has no stale choice after its queue drains"),BotPlayer->LevelUpOffer.IsValid());
    TestEqual(TEXT("Bot owns one personal perk stack for each earned level"),MCPlaytestBotsTestsPrivate::PerkStacks(BotPlayer),3);
    TestEqual(TEXT("Bot automation never consumes the human's queue"),Player->PendingLevelChoices,3);
    TestEqual(TEXT("Human choice still gates the main event"),Fixture.State->SingleDayDirector->Stage,EMCSingleDayStage::FirstPerk);
    TestEqual(TEXT("Personal AI grants keep the team mask clear"),Fixture.State->TeamToolUpgrades,uint8(0));

    for(int32 Index=0;Index<3;++Index)
        if(!TestTrue(TEXT("Human chooses each personal level independently"),
            Player->TryChooseLevelUpPerk(Player->LevelUpOffer.OfferId,0))) return false;
    TestFalse(TEXT("The complete coop team has released the card gate"),Progression->HasPendingChoices());
    TestEqual(TEXT("The human owns exactly its three selected stacks"),MCPlaytestBotsTestsPrivate::PerkStacks(Player),3);
    Fixture.Step(.2f);
    TestEqual(TEXT("Real sequence ticks launch nuts after the last human choice"),Fixture.State->SingleDayDirector->Stage,EMCSingleDayStage::Nuts);
    if(!TestNotNull(TEXT("Coop launches the real nut event"),Fixture.State->SingleDayDirector->NutEvent.Get())) return false;
    TestFalse(TEXT("Coop rain setup succeeds"),Fixture.State->SingleDayDirector->NutEvent->bFailed);

    Session->StopSession();
    TestEqual(TEXT("Coop stop removes the bot controller"),Fixture.Bots().Num(),0);
    TestFalse(TEXT("Coop stop removes its bot identity"),Fixture.State->PlayerArray.Contains(BotPlayer));
    TestNotNull(TEXT("Coop stop restores a playable human pawn"),Cast<AMCToothCharacter>(Human->GetPawn()));
    TestTrue(TEXT("Coop stop restores the normal lesson"),Fixture.State->bTutorialActive);
    TestEqual(TEXT("Coop stop restores training stage"),Fixture.State->SingleDayDirector->Stage,EMCSingleDayStage::Training);
    TestEqual(TEXT("Coop stop resets human personal perks"),MCPlaytestBotsTestsPrivate::PerkStacks(Player),0);
    TestEqual(TEXT("Coop stop clears the human's old pending choices"),Player->PendingLevelChoices,0);
    TestEqual(TEXT("Coop stop resets shared XP"),Progression->TotalExperience,int64(0));
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
