#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCRewardChest.h"
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
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardClaimTest,"MessControl.Rewards.StageBooster.SingleAuthoritativeClaim",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardClaimTest::RunTest(const FString&)
{
    FStageRewardWorld W;auto* A=W.Player(FVector(160,0,10060));auto* B=W.Player(FVector(-160,0,10060));
    auto* Chest=W.Chest();
    TestTrue(TEXT("Stage rewards use the distinct shared mode"),Chest->bTeamStageBooster);
    TestFalse(TEXT("Even a chance-one chest cannot become a stage mimic"),Chest->bMimic);
    Chest->ForceMimicForTest();TestFalse(TEXT("Debug mimic override cannot infect a stage reward"),Chest->bMimic);
    Chest->LootIDs={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
    Chest->Stage=EMCRewardChestStage::Open;Chest->OpeningPlayer=A;A->RewardInteraction=Chest;
    TestFalse(TEXT("A second player cannot claim the first player's open choice"),Chest->TryChooseCard(B,2));
    TestFalse(TEXT("An invalid card index cannot consume the reward"),Chest->TryChooseCard(A,3));
    TestTrue(TEXT("The designated opener activates the shared booster"),Chest->TryChooseCard(A,2));
    TestTrue(TEXT("Both teammates receive the choice"),W.Perks(A)->bGrenadeArmed && W.Perks(B)->bGrenadeArmed);
    TestEqual(TEXT("The opener advances chest reward history without XP"),A->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,1);
    TestEqual(TEXT("A living teammate also advances chest reward history"),B->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,1);
    TestEqual(TEXT("The shared chest is exhausted"),Chest->Stage,EMCRewardChestStage::Exhausted);
    TestEqual(TEXT("A claimed stage chest immediately releases its gameplay collision"),Chest->Solid->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestTrue(TEXT("A spawned claimed chest is scheduled for removal after its short cosmetic burst"),Chest->GetLifeSpan()>0.f && Chest->GetLifeSpan()<1.f);
    W.Perks(A)->bGrenadeArmed=W.Perks(B)->bGrenadeArmed=false;
    TestFalse(TEXT("Duplicate RPC cannot replay the team effect"),Chest->TryChooseCard(A,2));
    TestFalse(TEXT("A duplicate request leaves effects unchanged"),W.Perks(A)->bGrenadeArmed || W.Perks(B)->bGrenadeArmed);
    TestEqual(TEXT("A duplicate request cannot advance chest history"),A->GetPlayerState<AMCPlayerState>()->PersonalChoicesTaken,1);
    TestFalse(TEXT("The opener's authoritative interaction is released"),IsValid(A->RewardInteraction));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStageRewardPortionGateTest,"MessControl.Rewards.StageBooster.PortionCleanupAndDeduplication",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStageRewardPortionGateTest::RunTest(const FString&)
{
    FStageRewardWorld W;W.Player(FVector(3000,0,10000));
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
    return true;
}
#endif
