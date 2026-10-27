#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "MCArenaTooth.h"
#include "MCSingleDayDirector.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlaytestBotController.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCNutEnemy.h"
#include "MCMouthSurface.h"
#include "MCDayPlan.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FStageRewardWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    UDataTable* Table=nullptr;
    FStageRewardWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Game=World->SpawnActor<AMCGameState>();World->SetGameState(Game);
        Game->Phase=EMCShiftPhase::Working;Game->bLobbyWaiting=false;
        Table=NewObject<UDataTable>();Table->RowStruct=FMCPerkDefinition::StaticStruct();
        for(FName ID:{FName(TEXT("Fallback_Mouthwash")),FName(TEXT("Fallback_BrushFury")),FName(TEXT("Fallback_Grenade"))}) {
            FMCPerkDefinition Row;Row.bConsumableBooster=true;Row.MaxStacks=1000;
            Table->AddRow(ID,Row);
        }
    }
    ~FStageRewardWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCToothCharacter* Player(FVector Location,bool Bot=false)
    {
        AController* Controller=Bot?static_cast<AController*>(World->SpawnActor<AMCPlaytestBotController>())
            :static_cast<AController*>(World->SpawnActor<AMCPlayerController>());
        if(auto* Playtest=Cast<AMCPlaytestBotController>(Controller)) Playtest->SetLabControlled(true);
        const FTransform Pose;
        auto* State=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),Pose,Controller);
        State->Perks->PerkTable=Table;State->FinishSpawning(Pose);Controller->SetPlayerState(State);
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Location,FRotator::ZeroRotator);
        Controller->Possess(Hero);Hero->Status->Initialize(100);Hero->GetCharacterMovement()->DisableMovement();
        return Hero;
    }
    UMCPerkComponent* Perks(AMCToothCharacter* Hero) const {return Hero->GetPlayerState<AMCPlayerState>()->Perks;}
    AMCRewardChest* Chest(AActor* Owner=nullptr)
    {
        const FTransform Pose(FVector(0,0,10000));
        auto* Chest=World->SpawnActorDeferred<AMCRewardChest>(AMCRewardChest::StaticClass(),Pose,Owner,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Chest->MimicChance=1;
        Chest->InitializeTeamStageReward(Pose.GetLocation(),Pose.GetLocation()+FVector(0,0,450),31,Table,nullptr);
        Chest->FinishSpawning(Pose);Chest->Stage=EMCRewardChestStage::Landed;
        return Chest;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardImmediateFallTest,"MessControl.Rewards.StageBooster.ImmediateAirborneArrival",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardImmediateFallTest::RunTest(const FString&)
{
    FStageRewardWorld W;
    const FVector Landing(0,0,10000),Start=Landing+FVector(0,0,450);
    const FTransform Pose(Landing);
    auto* Chest=W.World->SpawnActorDeferred<AMCRewardChest>(AMCRewardChest::StaticClass(),Pose,
        nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Chest->InitializeTeamStageReward(Landing,Start,31,W.Table,nullptr);
    TestEqual(TEXT("A stage reward skips the ordinary telegraph delay"),Chest->Stage,EMCRewardChestStage::Falling);
    Chest->FinishSpawning(Pose);
    TestEqual(TEXT("BeginPlay preserves the immediate falling state"),Chest->Stage,EMCRewardChestStage::Falling);
    TestTrue(TEXT("FinishSpawning cannot leave a first-frame chest on the floor"),Chest->GetActorLocation().Equals(Start,.01));
    TestFalse(TEXT("The removed ground marker is never shown"),Chest->Telegraph->GetVisibleFlag());
    TestFalse(TEXT("No claim floor ring is shown"),Chest->ClaimRing->GetVisibleFlag());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardSharedEffectTest,"MessControl.Rewards.StageBooster.SharedImmediateEffects",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardSharedEffectTest::RunTest(const FString&)
{
    FStageRewardWorld W;
    auto* A=W.Player(FVector(-120,0,10000));auto* B=W.Player(FVector(120,0,10000));
    auto* Spectator=W.Player(FVector(2000,0,10000));Spectator->GetPlayerState<AMCPlayerState>()->SetIsSpectator(true);
    A->Status->ApplyCoffee();B->Status->ApplyCoffee();
    auto* Enemy=W.World->SpawnActor<AMCNutEnemy>(FVector(0,200,10000),FRotator::ZeroRotator);
    TestTrue(TEXT("Mouthwash activates immediately"),W.Perks(A)->ActivateTeamStageBooster(TEXT("Fallback_Mouthwash")));
    TestEqual(TEXT("Overlapping team radii damage the same enemy once"),Enemy->Health,55.f);
    TestEqual(TEXT("First teammate is cleansed"),A->Status->State.CoffeeLeft,0);
    TestEqual(TEXT("Second teammate is cleansed"),B->Status->State.CoffeeLeft,0);
    TestTrue(TEXT("Team grenade arms immediately"),W.Perks(A)->ActivateTeamStageBooster(TEXT("Fallback_Grenade")));
    TestTrue(TEXT("Both teammates receive the armed effect"),W.Perks(A)->bGrenadeArmed && W.Perks(B)->bGrenadeArmed);
    TestFalse(TEXT("Spectators receive no team effect"),W.Perks(Spectator)->bGrenadeArmed);
    TestTrue(TEXT("Team fury activates immediately"),W.Perks(A)->ActivateTeamStageBooster(TEXT("Fallback_BrushFury")));
    TestTrue(TEXT("Both teammates enter the existing five-second effect"),W.Perks(A)->IsFuryActive() && W.Perks(B)->IsFuryActive());
    TestTrue(TEXT("Immediate rewards add no consumable charges or permanent perk stacks"),W.Perks(A)->BoosterQueue.IsEmpty()
        && W.Perks(B)->BoosterQueue.IsEmpty() && W.Perks(A)->ActivePerks.IsEmpty() && W.Perks(B)->ActivePerks.IsEmpty());
    TestFalse(TEXT("A client-supplied unsupported perk is rejected"),W.Perks(A)->ActivateTeamStageBooster(TEXT("P01")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardIndividualClaimTest,"MessControl.Rewards.StageBooster.IndividualAuthoritativeClaims",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardIndividualClaimTest::RunTest(const FString&)
{
    FStageRewardWorld W;auto* A=W.Player(FVector(160,0,10060));auto* B=W.Player(FVector(-160,0,10060));
    auto* Chest=W.Chest();
    TestTrue(TEXT("Stage rewards use their distinct participant-ledger mode"),Chest->bTeamStageBooster);
    TestFalse(TEXT("Even a chance-one chest cannot become a stage mimic"),Chest->bMimic);
    Chest->ForceMimicForTest();TestFalse(TEXT("Debug mimic override cannot infect a stage reward"),Chest->bMimic);
    Chest->LootIDs={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
    Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
    TestFalse(TEXT("An entitled player must have an actual published interaction"),Chest->TryChooseCard(B,2));
    TestTrue(TEXT("First player receives a personal offer"),Chest->PublishStageChoiceFor(A));
    TestTrue(TEXT("A distant teammate also receives their own offer"),Chest->PublishStageChoiceFor(B));
    TestTrue(TEXT("Presentation receipt accepts the current recipient pawn"),Chest->ConfirmStageChoicePresented(A));
    TestTrue(TEXT("Confirmed cards remain acknowledged"),Chest->StageChoicePresented.Contains(A->GetPlayerState<AMCPlayerState>()));
    TestFalse(TEXT("An invalid card index cannot consume the reward"),Chest->TryChooseCard(A,3));
    TestTrue(TEXT("The first player activates their chosen booster"),Chest->TryChooseCard(A,2));
    TestTrue(TEXT("Only the first player is armed"),W.Perks(A)->bGrenadeArmed && !W.Perks(B)->bGrenadeArmed);
    TestEqual(TEXT("The opener advances chest reward history without XP"),A->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,1);
    TestEqual(TEXT("The unchosen teammate has not received a reward"),B->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,0);
    TestEqual(TEXT("The chest waits for the second player's independent choice"),Chest->Stage,EMCRewardChestStage::Open);
    W.Perks(A)->bGrenadeArmed=false;
    TestFalse(TEXT("Duplicate RPC cannot replay the first player's effect"),Chest->TryChooseCard(A,2));
    TestFalse(TEXT("A duplicate request leaves effects unchanged"),W.Perks(A)->bGrenadeArmed);
    TestEqual(TEXT("A duplicate request cannot advance chest history"),A->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,1);
    TestFalse(TEXT("The opener's authoritative interaction is released"),IsValid(A->RewardInteraction));
    TestTrue(TEXT("The second player can choose a different booster"),Chest->TryChooseCard(B,1));
    TestTrue(TEXT("Only the second player gets fury"),W.Perks(B)->IsFuryActive() && !W.Perks(A)->IsFuryActive());
    TestEqual(TEXT("The second player records exactly their own reward"),B->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,1);
    TestEqual(TEXT("Only the final resolved choice exhausts the chest"),Chest->Stage,EMCRewardChestStage::Exhausted);
    TestEqual(TEXT("The exhausted chest releases its gameplay collision"),Chest->Solid->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Removal retains the existing short cosmetic burst"),Chest->GetLifeSpan()>0.f && Chest->GetLifeSpan()<1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardReplacementTest,"MessControl.Rewards.StageBooster.DeathReplacementAndDeparture",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardReplacementTest::RunTest(const FString&)
{
    FStageRewardWorld W;auto* A=W.Player(FVector(160,0,10060));auto* B=W.Player(FVector(-160,0,10060));
    auto* Reserve=W.World->SpawnActor<AMCArenaTooth>(FVector(6000,0,10000),FRotator::ZeroRotator);
    W.World->GetGameState<AMCGameState>()->ArenaTeeth.Add(Reserve);
    auto* Leaving=W.Player(FVector(2000,0,10060));
    auto* PC=CastChecked<AMCPlayerController>(A->GetController());auto* State=A->GetPlayerState<AMCPlayerState>();
    auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
    Chest->LootIDs={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
    Chest->PublishStageChoiceFor(A);Chest->PublishStageChoiceFor(B);Chest->PublishStageChoiceFor(Leaving);
    A->Status->State.Health=0;Chest->PollTeamBoosterApproach();
    TestFalse(TEXT("Death releases the old pawn's reward lock"),IsValid(A->RewardInteraction));
    TestTrue(TEXT("Death does not consume the stable recipient entitlement"),Chest->StageRecipients.Contains(State) && !Chest->StageClaims.Contains(State));
    TestTrue(TEXT("Another player can resolve while recovery is pending"),Chest->TryChooseCard(B,2));
    Leaving->GetController()->Destroy();Chest->PollTeamBoosterApproach();
    TestEqual(TEXT("A disconnected recipient no longer blocks completion"),Chest->StageRecipients.Num(),2);
    auto* Late=W.Player(FVector(2200,0,10060));
    TestFalse(TEXT("A late join cannot silently enlarge an already issued reward"),Chest->PublishStageChoiceFor(Late));
    PC->UnPossess();
    auto* Replacement=W.World->SpawnActor<AMCToothCharacter>(FVector(160,0,10060),FRotator::ZeroRotator);
    PC->Possess(Replacement);Replacement->Status->Initialize(100);Replacement->GetCharacterMovement()->DisableMovement();
    Chest->PollTeamBoosterApproach();
    TestTrue(TEXT("The replacement pawn receives the still-unclaimed offer"),Chest->HasStageChoiceFor(Replacement));
    TestTrue(TEXT("The replacement can finish its own choice"),Chest->TryChooseCard(Replacement,2));
    TestEqual(TEXT("A replacement earns one reward, not a new identity's extra reward"),State->PersonalChoicesTaken,1);
    TestEqual(TEXT("No departed or old pawn keeps the chest stuck"),Chest->Stage,EMCRewardChestStage::Exhausted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardTerminalDeathTest,"MessControl.Rewards.StageBooster.TerminalDeathCannotBlockSurvivors",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardTerminalDeathTest::RunTest(const FString&)
{
    FStageRewardWorld W;auto* Survivor=W.Player(FVector(160,0,10060));auto* Dead=W.Player(FVector(2000,0,10060));
    auto* DeadState=Dead->GetPlayerState<AMCPlayerState>();
    auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
    Chest->LootIDs={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
    Chest->PublishStageChoiceFor(Survivor);Chest->PublishStageChoiceFor(Dead);
    Dead->Status->State.Health=0;
    TestEqual(TEXT("The fixture has no replacement tooth"),W.World->GetGameState<AMCGameState>()->AvailableArenaTeeth(),0);
    Chest->PollTeamBoosterApproach();
    TestEqual(TEXT("An unrecoverable dead participant cannot remain in the reward barrier"),Chest->StageRecipients.Num(),1);
    TestFalse(TEXT("The dead player's input interaction is released"),IsValid(Dead->RewardInteraction));
    TestEqual(TEXT("Waiving an impossible choice grants no phantom reward"),DeadState->PersonalChoicesTaken,0);
    TestTrue(TEXT("The survivor can finish its own reward"),Chest->TryChooseCard(Survivor,2));
    TestEqual(TEXT("The completed portion is not stuck on a player with no reserves"),Chest->Stage,EMCRewardChestStage::Exhausted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardBotSelectionTest,"MessControl.Rewards.StageBooster.BotsRespectHumanOwner",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardBotSelectionTest::RunTest(const FString&)
{
    FStageRewardWorld W;W.Player(FVector(1200,0,10000),true);W.Player(FVector(-1200,0,10000),true);
    auto* Human=W.Player(FVector(3000,0,10000));Human->Status->State.Health=0;
    auto* Chest=W.Chest();Chest->PollTeamBoosterApproach();
    TestEqual(TEXT("Bots never steal a recovering human's shared choice"),Chest->Stage,EMCRewardChestStage::Landed);
    TestEqual(TEXT("No reward is consumed while a human participates"),Chest->ClaimedMask,uint8(0));
    Human->GetController()->Destroy();Chest->PollTeamBoosterApproach();
    TestEqual(TEXT("A bot-only observation run resolves after landing without a fake UI controller"),Chest->Stage,EMCRewardChestStage::Exhausted);
    TestEqual(TEXT("The bot-only shared reward is claimed exactly once"),Chest->ClaimedMask,uint8(7));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardTimeoutTest,"MessControl.Rewards.StageBooster.FortySecondAutoChoice",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardTimeoutTest::RunTest(const FString&)
{
    {
        FStageRewardWorld W;W.World->TimeSeconds=100.;
        auto* A=W.Player(FVector(160,0,10060));auto* B=W.Player(FVector(-160,0,10060));
        auto* StateA=A->GetPlayerState<AMCPlayerState>();auto* StateB=B->GetPlayerState<AMCPlayerState>();
        StateA->SetPlayerId(101);StateB->SetPlayerId(102);
        auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
        Chest->StageStartedAt=Chest->ServerNow();
        Chest->LootIDs={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
        Chest->PublishStageChoiceFor(A);Chest->PublishStageChoiceFor(B);
        const double Deadline=Chest->GetStageChoiceDeadline(StateA);
        TestEqual(TEXT("The window is exactly forty server seconds from Open"),Deadline,140.);
        TestEqual(TEXT("Every captured recipient has the same finite stage window"),Chest->GetStageChoiceDeadline(StateB),Deadline);
        W.World->TimeSeconds=110.;
        Chest->ConfirmStageChoicePresented(A);Chest->PublishStageChoiceFor(A);
        TestEqual(TEXT("Acknowledgement and re-sending cards cannot extend the window"),Chest->GetStageChoiceDeadline(StateA),Deadline);
        TestTrue(TEXT("A manual choice before expiry still applies normally"),Chest->TryChooseCard(A,2));
        TestTrue(TEXT("The selected pre-deadline grenade is armed"),W.Perks(A)->bGrenadeArmed);
        W.World->TimeSeconds=139.99;Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("A pending player is not chosen early"),StateB->PersonalChoicesTaken,0);
        TestEqual(TEXT("The remaining deadline is not reset by the other player's choice"),Chest->GetStageChoiceDeadline(StateB),Deadline);
        B->Status->ApplyCoffee();
        W.World->TimeSeconds=140.;Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("Expiry resolves the AFK recipient without a button or presentation acknowledgement"),StateB->PersonalChoicesTaken,1);
        const int32 Effects=int32(W.Perks(B)->bGrenadeArmed)+int32(W.Perks(B)->IsFuryActive())+int32(B->Status->State.CoffeeLeft==0);
        TestEqual(TEXT("The server activates exactly one effect from the three offered cards"),Effects,1);
        TestEqual(TEXT("The last timed selection exhausts the stage chest"),Chest->Stage,EMCRewardChestStage::Exhausted);
        TestFalse(TEXT("A delayed manual request cannot replace or duplicate the automatic choice"),Chest->TryChooseCard(B,2));
        Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("Repeated expiry polling never records another reward"),StateB->PersonalChoicesTaken,1);
    }
    {
        FStageRewardWorld W;W.World->TimeSeconds=100.;
        auto* Game=W.World->GetGameState<AMCGameState>();Game->GameplayLoadId=FGuid::NewGuid();
        auto* Alive=W.Player(FVector(-160,0,10060));auto* Dead=W.Player(FVector(160,0,10060));
        auto* State=Dead->GetPlayerState<AMCPlayerState>();auto* PC=CastChecked<AMCPlayerController>(Dead->GetController());
        auto* Reserve=W.World->SpawnActor<AMCArenaTooth>(FVector(6000,0,10000),FRotator::ZeroRotator);
        Game->ArenaTeeth.Add(Reserve);
        auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
        Chest->StageStartedAt=Chest->ServerNow();
        Chest->LootIDs={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
        Chest->PublishStageChoiceFor(Alive);Chest->PublishStageChoiceFor(Dead);
        TestTrue(TEXT("The survivor resolves their normal personal choice"),Chest->TryChooseCard(Alive,2));
        const double Deadline=Chest->GetStageChoiceDeadline(State);
        W.World->TimeSeconds=110.;Dead->Status->State.Health=0;Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("Death does not restart the recipient's window"),Chest->GetStageChoiceDeadline(State),Deadline);
        TestEqual(TEXT("Death before expiry does not itself choose a reward"),State->PersonalChoicesTaken,0);
        PC->UnPossess();W.World->TimeSeconds=120.;
        auto* EarlyReplacement=W.World->SpawnActor<AMCToothCharacter>(FVector(160,0,10060),FRotator::ZeroRotator);
        PC->Possess(EarlyReplacement);EarlyReplacement->Status->Initialize(100);EarlyReplacement->GetCharacterMovement()->DisableMovement();
        Chest->PollTeamBoosterApproach();
        TestTrue(TEXT("A replacement before expiry receives the still-pending cards"),Chest->HasStageChoiceFor(EarlyReplacement));
        TestEqual(TEXT("Replacement and its new presentation preserve the original deadline"),Chest->GetStageChoiceDeadline(State),Deadline);
        EarlyReplacement->Status->State.Health=0;
        W.World->TimeSeconds=140.;Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("An expired dead player resolves the stage barrier immediately"),Chest->Stage,EMCRewardChestStage::Exhausted);
        TestEqual(TEXT("The recovering player still receives exactly one selected reward"),State->PersonalChoicesTaken,1);
        TestTrue(TEXT("The deferred choice is not put in the manual booster-charge queue"),State->Perks->BoosterQueue.IsEmpty());
        PC->UnPossess();
        auto* Replacement=W.World->SpawnActor<AMCToothCharacter>(FVector(160,0,10060),FRotator::ZeroRotator);
        PC->Possess(Replacement);Replacement->Status->Initialize(100);Replacement->GetCharacterMovement()->DisableMovement();
        Replacement->Status->ApplyCoffee();
        auto* Perks=State->Perks.Get();Perks->TickRogueBoosters(.1f);
        const int32 Effects=int32(Perks->bGrenadeArmed)+int32(Perks->IsFuryActive())+int32(Replacement->Status->State.CoffeeLeft==0);
        TestEqual(TEXT("The selected effect automatically reaches the eligible replacement body"),Effects,1);
        Perks->bGrenadeArmed=false;Perks->FuryUntil=-100;Replacement->Status->ApplyCoffee();
        Perks->TickRogueBoosters(.1f);
        TestTrue(TEXT("A deferred effect is consumed once rather than repeated each tick"),
            !Perks->bGrenadeArmed && !Perks->IsFuryActive() && Replacement->Status->State.CoffeeLeft>0);
    }
    {
        FStageRewardWorld W;auto* Hero=W.Player(FVector(160,0,10060));auto* Perks=W.Perks(Hero);
        auto* Game=W.World->GetGameState<AMCGameState>();Game->GameplayLoadId=FGuid::NewGuid();
        Hero->Status->State.Health=0;
        TestTrue(TEXT("An unavailable body can retain an expired selection outside the chest lifetime"),Perks->AcceptTimedStageBooster(TEXT("Fallback_Grenade")));
        Game->GameplayLoadId=FGuid::NewGuid();Hero->Status->Initialize(100);Perks->TickRogueBoosters(.1f);
        TestFalse(TEXT("A deferred effect cannot leak into a different run epoch"),Perks->bGrenadeArmed);
        Hero->Status->State.Health=0;
        TestTrue(TEXT("Another valid deferred effect can be accepted"),Perks->AcceptTimedStageBooster(TEXT("Fallback_Grenade")));
        Perks->ResetPerks();Hero->Status->Initialize(100);Perks->TickRogueBoosters(.1f);
        TestFalse(TEXT("ResetPerks also discards unactivated stage effects"),Perks->bGrenadeArmed);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardArrivalOverlapTest,"MessControl.Rewards.StageBooster.PlayersCannotBlockCentralArrival",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardArrivalOverlapTest::RunTest(const FString&)
{
    FStageRewardWorld W;
    auto* Floor=W.World->SpawnActor<AActor>();
    auto* Support=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Support);Floor->AddInstanceComponent(Support);
    Support->SetBoxExtent(FVector(500,500,25));Support->SetCollisionProfileName(TEXT("BlockAll"));
    Support->SetHiddenInGame(false);Support->SetVisibility(true);Support->RegisterComponent();
    Floor->SetActorLocation(FVector(0,0,9975));
    auto* Player=W.Player(FVector(0,0,10070));
    auto* Zone=W.World->SpawnActor<AMCRewardDropZone>(FVector(0,0,10000),FRotator::ZeroRotator);
    Zone->AllowedLandingSurface=Floor;Zone->InwardMargin=30;
    auto* Chest=W.Chest();const FVector Extent=Chest->GetPlacementHalfExtent();
    Zone->Area->SetBoxExtent(FVector(Extent.X+35,Extent.Y+35,200));
    FVector Landing,Normal;FRandomStream Blocked(31),Ignored(31);
    const TArray<AActor*> None,Players={Player};
    TestFalse(TEXT("An ordinary central reward still rejects player overlap"),Zone->FindLanding(Blocked,Extent,450,None,Landing,Normal));
    TestTrue(TEXT("Stage placement can use the actual floor under a waiting player"),Zone->FindLanding(Ignored,Extent,450,Players,Landing,Normal));
    Chest->DropZone=Zone;Chest->LandingPoint=FVector(0,0,10003);Chest->SetActorLocation(Chest->LandingPoint);
    TestTrue(TEXT("Waiting in the center cannot defer the stage chest touchdown"),Chest->HasClearLanding());
    Chest->Stage=EMCRewardChestStage::Landed;Chest->RefreshPresentation();
    TestEqual(TEXT("A stage chest cannot shove or trap the overlapping player"),Chest->Solid->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
    TestEqual(TEXT("A stage chest also cannot shove a ragdoll"),Chest->Solid->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Ignore);
    Chest->bTeamStageBooster=false;
    Chest->RefreshPresentation();
    TestEqual(TEXT("Ordinary chests keep physical-body collision"),Chest->Solid->GetCollisionResponseToChannel(ECC_PhysicsBody),ECR_Block);
    TestFalse(TEXT("An ordinary chest retains its occupied-landing protection"),Chest->HasClearLanding());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardPortionGateTest,"MessControl.Rewards.StageBooster.PortionCleanupAndDeduplication",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardPortionGateTest::RunTest(const FString&)
{
    FStageRewardWorld W;auto* Worker=W.Player(FVector(3000,0,10000));
    auto* Director=W.World->SpawnActor<AMCSingleDayDirector>();
    Director->Settings=NewObject<UMCSingleDayProfile>(Director);Director->Mechanics=NewObject<UMCDayPlan>(Director);
    Director->Stage=EMCSingleDayStage::FirstMeal;Director->MealPortionNumber=1;Director->MealPortionCount=3;
    Director->MealPortionTarget=Director->MealPortionSpawned=3;
    auto* Dirt=W.World->SpawnActor<AMCMouthSurface>(FVector(2000,1000,10000),FRotator::ZeroRotator);
    Dirt->bUlcer=false;Dirt->Status->ApplyCoffee();Director->TickFirstMeal();
    TestFalse(TEXT("Even a non-final portion waits for actual dirt cleanup"),Director->bStageRewardPending);
    Dirt->ClearLiquidForBooster();Director->TickFirstMeal();
    TestTrue(TEXT("A fully completed portion enters the shared reward gate"),Director->bStageRewardPending);
    TestEqual(TEXT("The portion completion has a stable identity"),Director->StageRewardCompletionId,FName(TEXT("OpeningPortion_1")));
    Director->TickFirstMeal();
    TestEqual(TEXT("Repeated completion polls retain the same reward"),Director->CompletedStageRewards.Num(),0);
    auto* Chest=W.Chest(Director);Director->StageRewardChest=Chest;Chest->ClaimedMask=7;
    Director->NotifyStageRewardFinished(Chest,false);Director->NotifyStageRewardFinished(Chest,false);
    TestEqual(TEXT("Repeated completion callbacks seal one reward identity"),Director->CompletedStageRewards.Num(),1);
    TestFalse(TEXT("A completed edge cannot enqueue another reward"),Director->BeginStageReward(TEXT("OpeningPortion_1"),EMCSingleDayRewardContinuation::OpeningCoffee));
    Director->ResetStageRewards();
    TestFalse(TEXT("Run reset removes the reward wait"),Director->bStageRewardPending);
    TestTrue(TEXT("Run reset clears the completion ledger"),Director->CompletedStageRewards.IsEmpty());
    Chest->Destroy();

    // Use the real producer and support validator: a static obstruction covers
    // the compact search, while nearby tongue remains a valid immediate drop.
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
    if(Plane) FStaticMeshCompilingManager::Get().FinishCompilation({Plane});
#endif
    if(!TestNotNull(TEXT("The nearby-stage-drop fixture has actual tissue geometry"),Plane)) return false;
    const FTransform TonguePose(FQuat::Identity,FVector(0,0,10000),FVector(20,20,1));
    auto* Tongue=W.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),TonguePose);
    Tongue->SourceMesh=Plane;Tongue->FinishSpawning(TonguePose);Tongue->SetActorTickEnabled(false);
    auto* Obstacle=W.World->SpawnActor<AActor>();
    auto* Block=NewObject<UBoxComponent>(Obstacle);Obstacle->SetRootComponent(Block);
    Block->SetBoxExtent(FVector(100,100,160));Block->SetCollisionProfileName(TEXT("BlockAll"));Block->RegisterComponent();
    Obstacle->SetActorLocation(FVector(0,0,10150));
    const FVector WorkerBefore=Worker->GetActorLocation();
    W.World->GetGameState<AMCGameState>()->RunSeed=73;
    TestTrue(TEXT("A new completed stage requests its reward normally"),
        Director->BeginStageReward(TEXT("NearbySupportedLanding"),EMCSingleDayRewardContinuation::OpeningCoffee));
    auto* NearbyChest=Director->StageRewardChest.Get();
    if(TestNotNull(TEXT("Nearby valid support produces a chest in the completion call without waiting another second"),NearbyChest)) {
        TestEqual(TEXT("Expanded placement does not add a later director retry"),Director->StageRewardPlacementAttempts,1);
        TestEqual(TEXT("The same-frame fallback retains immediate airborne arrival"),NearbyChest->Stage,EMCRewardChestStage::Falling);
        const FVector Extent=NearbyChest->GetPlacementHalfExtent(),Landing=NearbyChest->LandingPoint;
        TestTrue(TEXT("The landing stays outside the unchanged static obstruction"),
            FMath::Abs(Landing.X)>100+Extent.X-1 || FMath::Abs(Landing.Y)>100+Extent.Y-1);
        TestTrue(TEXT("The retained private drop zone still contains the full touchdown footprint"),
            Director->StageRewardZone->ContainsFootprint(Landing,Extent));
        TestTrue(TEXT("Placement never teleports the waiting player out of the center"),Worker->GetActorLocation().Equals(WorkerBefore));
        TestEqual(TEXT("The fallback leaves real obstacle collision enabled"),Block->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
    }
    Director->ResetStageRewards();
    return true;
}
#endif
