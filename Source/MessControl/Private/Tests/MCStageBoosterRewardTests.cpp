#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Algo/Count.h"
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
    FStageRewardWorld(bool AddDefaults=true)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Game=World->SpawnActor<AMCGameState>();World->SetGameState(Game);
        Game->Phase=EMCShiftPhase::Working;Game->bLobbyWaiting=false;
        Table=NewObject<UDataTable>();Table->RowStruct=FMCPerkDefinition::StaticStruct();
        if(AddDefaults) for(FName ID:{FName(TEXT("Fallback_Mouthwash")),FName(TEXT("Fallback_BrushFury")),FName(TEXT("Fallback_Grenade"))}) {
            FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Consumable;Row.bConsumableBooster=true;Row.MaxStacks=1;Row.InitialCharges=3;
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
    void Add(FName ID,EMCPerkKind Kind,bool Available=true,int32 Charges=3)
    {
        FMCPerkDefinition Row;Row.Kind=Kind;Row.MaxStacks=1;Row.InitialCharges=Charges;
        Row.bAvailableForLevelChoice=Available;Row.bConsumableBooster=Kind==EMCPerkKind::Consumable;Table->AddRow(ID,Row);
    }
    int32 Occupied(const UMCPerkComponent* Perks) const
    {return Algo::CountIf(Perks->ActiveSkillSlots,[](const FMCActiveSkillSlot& Slot){return !Slot.PerkID.IsNone();});}
    int32 Index(const AMCRewardChest* Chest,const AMCToothCharacter* Hero,FName ID) const
    {return Chest->GetStageOfferIDs(Hero->GetPlayerState<AMCPlayerState>()).IndexOfByKey(ID);}
    bool SameSlots(const UMCPerkComponent* Perks,const TArray<FMCActiveSkillSlot>& Before) const
    {
        if(Perks->ActiveSkillSlots.Num()!=Before.Num()) return false;
        for(int32 I=0;I<Before.Num();++I) {
            const auto& A=Perks->ActiveSkillSlots[I];const auto& B=Before[I];
            if(A.PerkID!=B.PerkID || A.InstanceID!=B.InstanceID || A.Charges!=B.Charges || A.CooldownEndsAt!=B.CooldownEndsAt) return false;
        }
        return true;
    }

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardSharedEffectTest,"MessControl.Rewards.StageBooster.LegacyTeamGrantStoresSkills",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardSharedEffectTest::RunTest(const FString&)
{
    FStageRewardWorld W;
    auto* A=W.Player(FVector(-120,0,10000));auto* B=W.Player(FVector(120,0,10000));
    auto* Spectator=W.Player(FVector(2000,0,10000));Spectator->GetPlayerState<AMCPlayerState>()->SetIsSpectator(true);
    A->Status->ApplyCoffee();B->Status->ApplyCoffee();
    auto* Enemy=W.World->SpawnActor<AMCNutEnemy>(FVector(0,200,10000),FRotator::ZeroRotator);
    TestTrue(TEXT("Legacy team mouthwash grants stored skills"),W.Perks(A)->ActivateTeamStageBooster(TEXT("Fallback_Mouthwash")));
    TestEqual(TEXT("Acquisition causes no burst damage"),Enemy->Health,90.f);
    TestTrue(TEXT("Acquisition does not cleanse either teammate"),A->Status->State.CoffeeLeft>0 && B->Status->State.CoffeeLeft>0);
    TestTrue(TEXT("Legacy team grenade grants stored skills"),W.Perks(A)->ActivateTeamStageBooster(TEXT("Fallback_Grenade")));
    TestFalse(TEXT("Neither teammate is armed by acquisition"),W.Perks(A)->bGrenadeArmed || W.Perks(B)->bGrenadeArmed);
    TestTrue(TEXT("Legacy team fury grants stored skills"),W.Perks(A)->ActivateTeamStageBooster(TEXT("Fallback_BrushFury")));
    TestFalse(TEXT("Neither teammate starts fury during acquisition"),W.Perks(A)->IsFuryActive() || W.Perks(B)->IsFuryActive());
    TestEqual(TEXT("Three unique grants occupy three slots per recipient"),W.Occupied(W.Perks(A))+W.Occupied(W.Perks(B)),6);
    TestEqual(TEXT("Spectators receive no stored grant"),W.Occupied(W.Perks(Spectator)),0);
    TestTrue(TEXT("Explicit slot activation runs mouthwash"),W.Perks(A)->ActivateSkillSlot(0,W.Perks(A)->ActiveSkillSlots[0].InstanceID));
    TestEqual(TEXT("One activation consumes one authored charge"),W.Perks(A)->ActiveSkillSlots[0].Charges,2);
    TestEqual(TEXT("The other recipient keeps all of its own charges"),W.Perks(B)->ActiveSkillSlots[0].Charges,3);
    TestEqual(TEXT("The activated burst damages a nearby enemy once"),Enemy->Health,55.f);
    TestTrue(TEXT("Explicit grenade activation succeeds"),W.Perks(A)->ActivateSkillSlot(1,W.Perks(A)->ActiveSkillSlots[1].InstanceID));
    TestTrue(TEXT("Only the activating player is armed"),W.Perks(A)->bGrenadeArmed && !W.Perks(B)->bGrenadeArmed);
    TestTrue(TEXT("Explicit fury activation succeeds"),W.Perks(A)->ActivateSkillSlot(2,W.Perks(A)->ActiveSkillSlots[2].InstanceID));
    TestTrue(TEXT("Only the activating player starts fury"),W.Perks(A)->IsFuryActive() && !W.Perks(B)->IsFuryActive());
    TestFalse(TEXT("A client-supplied unsupported perk is rejected"),W.Perks(A)->ActivateTeamStageBooster(TEXT("P01")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardIndividualClaimTest,"MessControl.Rewards.StageBooster.IndividualAuthoritativeClaims",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardIndividualClaimTest::RunTest(const FString&)
{
    FStageRewardWorld W;auto* A=W.Player(FVector(160,0,10060));auto* B=W.Player(FVector(-160,0,10060));
    auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
    TestTrue(TEXT("Stage rewards retain their participant-ledger mode"),Chest->bTeamStageBooster);
    Chest->ForceMimicForTest();TestFalse(TEXT("A stage reward cannot become a mimic"),Chest->bMimic);
    const int32 AIndex=W.Index(Chest,A,TEXT("Fallback_Grenade"));
    const int32 BIndex=W.Index(Chest,B,TEXT("Fallback_BrushFury"));
    TestTrue(TEXT("Each issued pool contains the available canonical rewards"),AIndex!=INDEX_NONE && BIndex!=INDEX_NONE);
    TestFalse(TEXT("An entitlement alone does not authorize a claim before presentation"),Chest->TryChooseCard(B,BIndex));
    TestTrue(TEXT("First recipient gets its personal offer"),Chest->PublishStageChoiceFor(A));
    TestTrue(TEXT("Second recipient gets its personal offer"),Chest->PublishStageChoiceFor(B));
    const auto* StateA=A->GetPlayerState<AMCPlayerState>();const auto* StateB=B->GetPlayerState<AMCPlayerState>();
    const FGuid AOffer=Chest->GetChoiceOfferID(StateA),BOffer=Chest->GetChoiceOfferID(StateB);
    TestTrue(TEXT("Receipts have distinct valid identities"),AOffer.IsValid() && BOffer.IsValid() && AOffer!=BOffer);
    TestTrue(TEXT("Current token and card validate"),Chest->ValidateChoiceOffer(A,AOffer,TEXT("Fallback_Grenade"),AIndex));
    TestFalse(TEXT("Another recipient token cannot authorize this card"),Chest->ValidateChoiceOffer(A,BOffer,TEXT("Fallback_Grenade"),AIndex));
    TestFalse(TEXT("A changed offered ID is rejected"),Chest->ValidateChoiceOffer(A,AOffer,TEXT("NotIssued"),AIndex));
    TestTrue(TEXT("Presentation acknowledgment is recipient-specific"),Chest->ConfirmStageChoicePresented(A));
    TestFalse(TEXT("Invalid card index cannot consume the receipt"),Chest->TryChooseCard(A,99));
    TestTrue(TEXT("First recipient acquires the selected item into a slot"),Chest->TryChooseCard(A,AIndex));
    TestTrue(TEXT("Only the first recipient owns the selected canonical ID"),W.Perks(A)->OwnsPerk(TEXT("Fallback_Grenade")) && !W.Perks(B)->OwnsPerk(TEXT("Fallback_Grenade")));
    TestEqual(TEXT("Acquisition stores the configured charges"),W.Perks(A)->ActiveSkillSlots[0].Charges,3);
    TestFalse(TEXT("Granting a stored grenade does not arm it immediately"),W.Perks(A)->bGrenadeArmed);
    TestEqual(TEXT("An actual acquisition advances only its recipient's history"),StateA->PersonalChoicesTaken,1);
    TestEqual(TEXT("The other player is still waiting"),StateB->PersonalChoicesTaken,0);
    TestEqual(TEXT("First selection does not close the other player's offer"),Chest->Stage,EMCRewardChestStage::Open);
    const auto Slot=W.Perks(A)->ActiveSkillSlots[0];
    TestFalse(TEXT("Repeated claim cannot replay the grant"),Chest->TryChooseCard(A,AIndex));
    TestTrue(TEXT("Repeated claim does not replace/reset the stored instance"),W.Perks(A)->ActiveSkillSlots[0].InstanceID==Slot.InstanceID);
    TestTrue(TEXT("Second player can acquire a different stored item"),Chest->TryChooseCard(B,BIndex));
    TestFalse(TEXT("Stored fury does not activate on acquisition"),W.Perks(B)->IsFuryActive());
    TestEqual(TEXT("Each personal acquisition uses one slot"),W.Occupied(W.Perks(B)),1);
    TestEqual(TEXT("Only the final resolution exhausts the chest"),Chest->Stage,EMCRewardChestStage::Exhausted);
    TestEqual(TEXT("Removal releases gameplay collision"),Chest->Solid->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Removal keeps its short cosmetic burst"),Chest->GetLifeSpan()>0.f && Chest->GetLifeSpan()<1.f);
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
    Chest->PublishStageChoiceFor(A);Chest->PublishStageChoiceFor(B);Chest->PublishStageChoiceFor(Leaving);
    const auto IDs=Chest->GetStageOfferIDs(State);const auto Receipt=Chest->GetChoiceOfferID(State);
    A->Status->State.Health=0;Chest->PollTeamBoosterApproach();
    TestFalse(TEXT("Death releases the old body's modal interaction"),IsValid(A->RewardInteraction));
    TestTrue(TEXT("A reserve keeps the original recipient entitlement"),Chest->StageRecipients.Contains(State) && !Chest->StageClaims.Contains(State));
    TestTrue(TEXT("Another player may finish while replacement is pending"),Chest->TryChooseCard(B,W.Index(Chest,B,TEXT("Fallback_Grenade"))));
    Leaving->GetController()->Destroy();Chest->PollTeamBoosterApproach();
    TestEqual(TEXT("Disconnected recipients stop blocking"),Chest->StageRecipients.Num(),2);
    auto* Late=W.Player(FVector(2200,0,10060));
    TestFalse(TEXT("A late join cannot enlarge an issued receipt group"),Chest->PublishStageChoiceFor(Late));
    PC->UnPossess();auto* Replacement=W.World->SpawnActor<AMCToothCharacter>(FVector(160,0,10060),FRotator::ZeroRotator);
    PC->Possess(Replacement);Replacement->Status->Initialize(100);Replacement->GetCharacterMovement()->DisableMovement();
    Chest->PollTeamBoosterApproach();
    TestTrue(TEXT("The replacement gets the still-pending offer"),Chest->HasStageChoiceFor(Replacement));
    TestTrue(TEXT("Death/reopen cannot reroll IDs"),Chest->GetStageOfferIDs(State)==IDs);
    TestTrue(TEXT("Death/reopen preserves the receipt identity"),Chest->GetChoiceOfferID(State)==Receipt);
    TestTrue(TEXT("Replacement completes its original choice"),Chest->TryChooseCard(Replacement,W.Index(Chest,Replacement,TEXT("Fallback_Grenade"))));
    TestEqual(TEXT("Replacement is one reward identity"),State->PersonalChoicesTaken,1);
    TestEqual(TEXT("No departed/old body leaves a deadlock"),Chest->Stage,EMCRewardChestStage::Exhausted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardTerminalDeathTest,"MessControl.Rewards.StageBooster.TerminalDeathCannotBlockSurvivors",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardTerminalDeathTest::RunTest(const FString&)
{
    FStageRewardWorld W;auto* Survivor=W.Player(FVector(160,0,10060));auto* Dead=W.Player(FVector(2000,0,10060));
    auto* DeadState=Dead->GetPlayerState<AMCPlayerState>();auto* Chest=W.Chest();
    Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
    Chest->PublishStageChoiceFor(Survivor);Chest->PublishStageChoiceFor(Dead);Dead->Status->State.Health=0;
    TestEqual(TEXT("No reserve exists in this fixture"),W.World->GetGameState<AMCGameState>()->AvailableArenaTeeth(),0);
    Chest->PollTeamBoosterApproach();
    TestEqual(TEXT("An unrecoverable recipient is waived"),Chest->StageRecipients.Num(),1);
    TestFalse(TEXT("Waived bodies keep no input interaction"),IsValid(Dead->RewardInteraction));
    TestFalse(TEXT("A waived receipt is no longer a valid token"),Chest->GetChoiceOfferID(DeadState).IsValid());
    TestEqual(TEXT("Waiving grants no phantom acquisition"),DeadState->PersonalChoicesTaken,0);
    TestTrue(TEXT("The survivor still finishes normally"),Chest->TryChooseCard(Survivor,W.Index(Chest,Survivor,TEXT("Fallback_Grenade"))));
    TestEqual(TEXT("The barrier is not stuck"),Chest->Stage,EMCRewardChestStage::Exhausted);return true;
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
        StateA->SetPlayerId(101);StateB->SetPlayerId(102);auto* Chest=W.Chest();
        Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;Chest->StageStartedAt=100.;
        Chest->PublishStageChoiceFor(A);Chest->PublishStageChoiceFor(B);
        const auto Issued=Chest->GetStageOfferIDs(StateB);const auto Token=Chest->GetChoiceOfferID(StateB);
        const double Deadline=Chest->GetStageChoiceDeadline(StateB);
        TestEqual(TEXT("The window is Open plus forty server seconds"),Deadline,140.);
        TestTrue(TEXT("A manual acquisition succeeds before expiry"),Chest->TryChooseCard(A,W.Index(Chest,A,TEXT("Fallback_Grenade"))));
        W.World->TimeSeconds=139.99;Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("The other player is not auto-chosen early"),StateB->PersonalChoicesTaken,0);
        TestEqual(TEXT("Another claim does not extend the deadline"),Chest->GetStageChoiceDeadline(StateB),Deadline);
        B->Status->ApplyCoffee();W.World->TimeSeconds=140.;Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("Expiry safely resolves an unacknowledged UI"),StateB->PersonalChoicesTaken,1);
        TestEqual(TEXT("Expiry stores exactly one skill instance"),W.Occupied(W.Perks(B)),1);
        TestTrue(TEXT("The auto-acquired ID came from that player's issued cards"),Issued.Contains(W.Perks(B)->ActiveSkillSlots[0].PerkID));
        TestTrue(TEXT("Receipt creation never activates/cleanses the player"),!W.Perks(B)->bGrenadeArmed && !W.Perks(B)->IsFuryActive() && B->Status->State.CoffeeLeft>0);
        const auto Snapshot=W.Perks(B)->ActiveSkillSlots;
        TestFalse(TEXT("An old receipt cannot authorize a late choice"),Chest->ValidateChoiceOffer(B,Token,Issued[0],0));
        Chest->PollTeamBoosterApproach();TestTrue(TEXT("Repeated expiry does not reset charges or instance"),W.SameSlots(W.Perks(B),Snapshot));
        TestEqual(TEXT("Final resolution exhausts the chest"),Chest->Stage,EMCRewardChestStage::Exhausted);
    }
    {
        FStageRewardWorld W;W.World->TimeSeconds=100.;
        auto* A=W.Player(FVector(160,0,10060));auto* Alive=W.Player(FVector(-160,0,10060));
        auto* Reserve=W.World->SpawnActor<AMCArenaTooth>(FVector(6000,0,10000),FRotator::ZeroRotator);W.World->GetGameState<AMCGameState>()->ArenaTeeth.Add(Reserve);
        auto* State=A->GetPlayerState<AMCPlayerState>();auto* PC=CastChecked<AMCPlayerController>(A->GetController());
        auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;Chest->StageStartedAt=100.;
        Chest->PublishStageChoiceFor(A);Chest->PublishStageChoiceFor(Alive);A->Status->State.Health=0;Chest->PollTeamBoosterApproach();
        TestTrue(TEXT("A survivor can finish during another replacement"),Chest->TryChooseCard(Alive,W.Index(Chest,Alive,TEXT("Fallback_Grenade"))));
        W.World->TimeSeconds=140.;Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("A dead recipient's timeout stores a reward on PlayerState"),W.Occupied(State->Perks),1);
        const auto Slots=State->Perks->ActiveSkillSlots;
        PC->UnPossess();auto* Replacement=W.World->SpawnActor<AMCToothCharacter>(FVector(160,0,10060),FRotator::ZeroRotator);
        PC->Possess(Replacement);Replacement->Status->Initialize(100);Replacement->GetCharacterMovement()->DisableMovement();
        State->Perks->TickRogueBoosters(.1f);
        TestTrue(TEXT("Recovery preserves the same stored charges and identity"),W.SameSlots(State->Perks,Slots));
        TestTrue(TEXT("Recovery does not auto-activate the acquired item"),!State->Perks->bGrenadeArmed && !State->Perks->IsFuryActive());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardOfferPoolTest,"MessControl.Rewards.StageBooster.PersonalSmallPoolsAndReceipts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardOfferPoolTest::RunTest(const FString&)
{
    {
        FStageRewardWorld W(false);W.Add(TEXT("PassiveA"),EMCPerkKind::Passive);W.Add(TEXT("PassiveB"),EMCPerkKind::Passive);
        auto* A=W.Player(FVector(160,0,10060));auto* B=W.Player(FVector(-160,0,10060));
        TestTrue(TEXT("The preexisting passive is acquired through the real core API"),W.Perks(A)->TryAcquirePerk(TEXT("PassiveA")));
        auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
        auto* StateA=A->GetPlayerState<AMCPlayerState>();auto* StateB=B->GetPlayerState<AMCPlayerState>();
        const auto AIDs=Chest->GetStageOfferIDs(StateA),BIDs=Chest->GetStageOfferIDs(StateB);
        TestEqual(TEXT("An owned passive leaves one valid card, not a padded duplicate"),AIDs.Num(),1);
        TestTrue(TEXT("Owned IDs are excluded canonically"),AIDs.Contains(TEXT("PassiveB")) && !AIDs.Contains(TEXT("PassiveA")));
        TestEqual(TEXT("Another player independently sees both available cards"),BIDs.Num(),2);
        TestTrue(TEXT("One-card offers publish"),Chest->PublishStageChoiceFor(A));TestTrue(TEXT("Two-card offers publish"),Chest->PublishStageChoiceFor(B));
        const auto Token=Chest->GetChoiceOfferID(StateA);Chest->PublishStageChoiceFor(A);
        TestTrue(TEXT("Republishing cannot reroll cards or receipt"),Chest->GetStageOfferIDs(StateA)==AIDs && Chest->GetChoiceOfferID(StateA)==Token);
        TestTrue(TEXT("A passive can be acquired without a numbered slot"),Chest->TryChooseCard(A,0));
        TestEqual(TEXT("Passive acquisition does not fill the active hotbar"),W.Occupied(W.Perks(A)),0);
        TestTrue(TEXT("Explicit skip resolves only the remaining recipient"),Chest->SkipChoice(B));
        TestEqual(TEXT("Skipping grants no reward history"),StateB->PersonalChoicesTaken,0);
        TestEqual(TEXT("Skip completes the team barrier"),Chest->Stage,EMCRewardChestStage::Exhausted);
    }
    {
        FStageRewardWorld W(false);W.Add(TEXT("OnlyPassive"),EMCPerkKind::Passive);auto* A=W.Player(FVector(160,0,10060));
        TestTrue(TEXT("The only catalog entry is already owned"),W.Perks(A)->TryAcquirePerk(TEXT("OnlyPassive")));
        auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;
        TestTrue(TEXT("An empty pool has no fabricated fallback"),Chest->GetStageOfferIDs(A->GetPlayerState<AMCPlayerState>()).IsEmpty());
        Chest->PollTeamBoosterApproach();
        TestEqual(TEXT("Zero eligible cards cannot deadlock the stage"),Chest->Stage,EMCRewardChestStage::Exhausted);
        TestEqual(TEXT("An empty pool grants no phantom reward"),A->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardSlotTransactionTest,"MessControl.Rewards.StageBooster.FullSlotsSkipAndStaleReplacement",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardSlotTransactionTest::RunTest(const FString&)
{
    auto Populate=[](FStageRewardWorld& W) {
        for(FName ID:{FName(TEXT("ActiveA")),FName(TEXT("ActiveB")),FName(TEXT("ActiveC")),FName(TEXT("ActiveD")),FName(TEXT("Other"))}) W.Add(ID,EMCPerkKind::Active,false);
        W.Add(TEXT("Offered"),EMCPerkKind::Active);
    };
    auto Fill=[this](UMCPerkComponent* Perks) {
        for(FName ID:{FName(TEXT("ActiveA")),FName(TEXT("ActiveB")),FName(TEXT("ActiveC")),FName(TEXT("ActiveD"))})
            TestTrue(TEXT("Four distinct active IDs acquire normally"),Perks->TryAcquirePerk(ID));
    };
    {
        FStageRewardWorld W(false);Populate(W);W.World->TimeSeconds=100.;auto* A=W.Player(FVector(160,0,10060));auto* Perks=W.Perks(A);Fill(Perks);
        const auto Before=Perks->ActiveSkillSlots;auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;Chest->StageStartedAt=100.;
        TestEqual(TEXT("Full capacity does not hide a replaceable offer"),Chest->GetStageOfferIDs(A->GetPlayerState<AMCPlayerState>()).Num(),1);
        TestTrue(TEXT("A full-slot offer still publishes"),Chest->PublishStageChoiceFor(A));
        W.World->TimeSeconds=140.;Chest->PollTeamBoosterApproach();
        TestTrue(TEXT("Timeout never destructively swaps or resets a full loadout"),W.SameSlots(Perks,Before));
        TestFalse(TEXT("The skipped skill was not granted"),Perks->OwnsPerk(TEXT("Offered")));
        TestEqual(TEXT("Timeout skip resolves the stage"),Chest->Stage,EMCRewardChestStage::Exhausted);
        TestEqual(TEXT("Timeout skip does not count as acquisition"),A->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,0);
    }
    {
        FStageRewardWorld W(false);Populate(W);auto* A=W.Player(FVector(160,0,10060));auto* Perks=W.Perks(A);Fill(Perks);
        auto* Chest=W.Chest();Chest->CaptureStageRecipients();Chest->Stage=EMCRewardChestStage::Open;Chest->PublishStageChoiceFor(A);
        const auto Old=Perks->ActiveSkillSlots[1];const auto Token=Chest->GetChoiceOfferID(A->GetPlayerState<AMCPlayerState>());
        TestTrue(TEXT("A temporary replacement uses the real core transaction"),Perks->TryAcquirePerk(TEXT("Other"),1,Old.PerkID,Old.InstanceID));
        const auto Middle=Perks->ActiveSkillSlots[1];
        TestTrue(TEXT("The same ID can return after removal"),Perks->TryAcquirePerk(Old.PerkID,1,Middle.PerkID,Middle.InstanceID));
        const auto Before=Perks->ActiveSkillSlots;
        TestFalse(TEXT("An ABA-reused ID cannot satisfy a stale slot instance"),Chest->TryChooseCard(A,0,1,Old.PerkID,Old.InstanceID));
        TestTrue(TEXT("A rejected replacement leaves every slot unchanged"),W.SameSlots(Perks,Before));
        TestTrue(TEXT("Rejection does not reroll or consume the personal receipt"),Chest->GetChoiceOfferID(A->GetPlayerState<AMCPlayerState>())==Token && Chest->HasStageChoiceFor(A));
        const auto Current=Perks->ActiveSkillSlots[1];
        TestTrue(TEXT("Fresh slot identity authorizes the explicit replacement"),Chest->TryChooseCard(A,0,1,Current.PerkID,Current.InstanceID));
        TestTrue(TEXT("Only the selected slot changed"),Perks->ActiveSkillSlots[1].PerkID==TEXT("Offered") && Perks->ActiveSkillSlots[0].InstanceID==Before[0].InstanceID && Perks->ActiveSkillSlots[2].InstanceID==Before[2].InstanceID && Perks->ActiveSkillSlots[3].InstanceID==Before[3].InstanceID);
        TestFalse(TEXT("The replaced ID is no longer owned"),Perks->OwnsPerk(Old.PerkID));
    }
    return true;
}

#endif
