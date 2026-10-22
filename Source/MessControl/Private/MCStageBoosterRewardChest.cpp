#include "MCRewardChest.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPlaytestBotController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

void AMCRewardChest::InitializeTeamStageReward(FVector Landing,FVector Start,int32 Seed,UDataTable* Table,AMCRewardDropZone* Zone)
{
    if(!HasAuthority()) return;
    bTeamStageBooster=true; bAlwaysRelevant=true; MimicChance=0; RogueKind=EMCRogueRewardKind::Ordinary;
    InitializeReward(Landing,Start,Seed,Table,EMCRewardSelectionPolicy::ChooseOne,Zone);
    ForceNetUpdate();
}

void AMCRewardChest::PollTeamBoosterApproach()
{
    // Stage rewards are the progression source. No XP/personal-level offer can
    // block a chest after a completed portion.
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!State || State->bLobbyWaiting || State->Phase==EMCShiftPhase::Lost || State->Phase==EMCShiftPhase::Won) return;
    TArray<AMCToothCharacter*> Nearby;
    bool HasHumanParticipant=false;
    for(TActorIterator<AMCPlayerController> It(GetWorld());It;++It) {
        const auto* PlayerState=It->GetPlayerState<AMCPlayerState>();
        // A dead/recovering human still owns this choice; bots must not steal it.
        if(!It->IsActorBeingDestroyed() && (!PlayerState || (!PlayerState->IsSpectator() && !PlayerState->IsOnlyASpectator()))) HasHumanParticipant=true;
        if(auto* Player=Cast<AMCToothCharacter>(It->GetPawn());IsLivingPlayer(Player) && Player->CanWork()
            && CanReachLockpick(Player) && !It->IsRewardInteractionActive()) Nearby.Add(Player);
    }
    if(!HasHumanParticipant) {
        // Observation runs made entirely of playtest bots have no human UI owner.
        // Resolve once through the same authoritative team effect after landing.
        for(TActorIterator<AMCPlaytestBotController> It(GetWorld());It;++It) {
            auto* Hero=Cast<AMCToothCharacter>(It->GetPawn());
            auto* PlayerState=It->GetPlayerState<AMCPlayerState>();
            if(!IsLivingPlayer(Hero) || !PlayerState || PlayerState->IsSpectator() || PlayerState->IsOnlyASpectator()
                || !BuildTeamBoosterOffer(PlayerState->Perks)) continue;
            ApplyTeamBoosterOnce(PlayerState->Perks,LootIDs[0]);break;
        }
        return;
    }
    Nearby.Sort([this](const AMCToothCharacter& A,const AMCToothCharacter& B) {
        const double DA=FVector::DistSquared(A.GetActorLocation(),GetActorLocation());
        const double DB=FVector::DistSquared(B.GetActorLocation(),GetActorLocation());
        return DA==DB?A.GetUniqueID()<B.GetUniqueID():DA<DB;
    });
    for(auto* Player:Nearby) if(BeginTeamBoosterChoice(Player)) break;
}

bool AMCRewardChest::BeginTeamBoosterChoice(AMCToothCharacter* Player)
{
    if(!HasAuthority() || !bTeamStageBooster || Stage!=EMCRewardChestStage::Landed || bClaimInProgress
        || ClaimedMask!=0 || !IsLivingPlayer(Player) || !Player->CanWork() || !CanReachLockpick(Player)
        || Player->GetWorld()!=GetWorld()
        || Player->HasFrozenLegs() || (Player->Inventory && Player->Inventory->bChainsawRunning)) return false;
    auto* PC=Cast<AMCPlayerController>(Player->GetController());
    const auto* PS=Player->GetPlayerState<AMCPlayerState>();
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if(!PC || PC->IsRewardInteractionActive() || !PS || !PS->Perks
        || PS->Perks->IsFuryActive() || !Game || Game->bLobbyWaiting || Game->Phase==EMCShiftPhase::Lost
        || Game->Phase==EMCShiftPhase::Won
        || !RewardTable || RewardTable->GetRowStruct()!=FMCPerkDefinition::StaticStruct()
        || PS->Perks->GetPerkTable()!=RewardTable) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCStageRewardApproach),true,this); Query.AddIgnoredActor(Player);
    FHitResult Block;
    if(GetWorld()->LineTraceSingleByChannel(Block,Player->GetActorLocation()+FVector(0,0,35),Solid->Bounds.Origin,ECC_Visibility,Query)) return false;
    if(!BuildTeamBoosterOffer(PS->Perks)) return false;
    // Set the authoritative interaction before cancelling input: the existing
    // food-preservation path recognizes this reward and retains carried food.
    OpeningPlayer=Player; Player->RewardInteraction=this; Player->ForceNetUpdate();
    SetStage(EMCRewardChestStage::Opening);
    Player->CancelGameplayInput();
    PC->ClientShowRewardOpening(this,StageStartedAt+FMath::Max(.1f,OpeningSeconds));
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_OPEN chest=%s player=%s"),*GetName(),*PS->GetPlayerName());
    return true;
}

bool AMCRewardChest::BuildTeamBoosterOffer(const UMCPerkComponent* Perks)
{
    if(!Perks || Perks->GetPerkTable()!=RewardTable || !RewardTable
        || RewardTable->GetRowStruct()!=FMCPerkDefinition::StaticStruct()) return false;
    TArray<FName> Offers={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
    for(FName ID:Offers) {
        const auto* Row=RewardTable->FindRow<FMCPerkDefinition>(ID,TEXT("Team stage booster"),false);
        if(!Row || !Row->bConsumableBooster || Row->Polarity!=EMCPerkPolarity::Positive) return false;
    }
    FRandomStream Roll(RollSeed);
    for(int32 I=Offers.Num()-1;I>0;--I) Offers.Swap(I,Roll.RandRange(0,I));
    LootIDs=MoveTemp(Offers); Polarity=EMCPerkPolarity::Positive;
    return true;
}

bool AMCRewardChest::TryChooseTeamBooster(AMCToothCharacter* Player,int32 Index)
{
    if(!HasAuthority() || !bTeamStageBooster || bClaimInProgress || ClaimedMask!=0
        || Stage!=EMCRewardChestStage::Open || Player!=OpeningPlayer || !OpenerCanContinue()
        || LootIDs.Num()!=3 || !LootIDs.IsValidIndex(Index)) return false;
    auto* PS=Player->GetPlayerState<AMCPlayerState>();
    const FName ID=LootIDs[Index];
    const auto* Row=RewardTable?RewardTable->FindRow<FMCPerkDefinition>(ID,TEXT("Claim team stage booster"),false):nullptr;
    if(!PS || !PS->Perks || PS->Perks->GetPerkTable()!=RewardTable || !Row || !Row->bConsumableBooster
        || !UMCPerkComponent::IsTeamStageBoosterID(ID)) return false;
    if(!ApplyTeamBoosterOnce(PS->Perks,ID)) return false;
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_CLAIM chest=%s player=%s booster=%s"),*GetName(),*PS->GetPlayerName(),*ID.ToString());
    return true;
}

bool AMCRewardChest::ApplyTeamBoosterOnce(UMCPerkComponent* Perks,FName ID)
{
    if(!HasAuthority() || !bTeamStageBooster || bClaimInProgress || ClaimedMask!=0 || !Perks
        || Perks->GetPerkTable()!=RewardTable || !LootIDs.Contains(ID) || !UMCPerkComponent::IsTeamStageBoosterID(ID)) return false;
    TGuardValue<bool> Guard(bClaimInProgress,true);
    // Seal before gameplay callbacks. Repeated RPCs and reentrant effects cannot
    // apply this shared reward a second time.
    ClaimedMask=7;
    if(!Perks->ActivateTeamStageBooster(ID)) {ClaimedMask=0;return false;}
    // Every living recipient actually earned this chest, including teammates
    // who did not click the card. Future optional pools use chest history now.
    for(TActorIterator<AMCPlayerState> It(GetWorld());It;++It) {
        auto* Hero=Cast<AMCToothCharacter>(It->GetPawn());
        if(!It->IsActorBeingDestroyed() && !It->IsSpectator() && !It->IsOnlyASpectator() && It->Perks
            && IsValid(Hero) && !Hero->IsActorBeingDestroyed() && Hero->GetController()
            && !Hero->GetController()->IsActorBeingDestroyed() && Hero->GetPlayerState<AMCPlayerState>()==*It
            && Hero->Status && Hero->Status->IsAlive()) It->RecordChestRewardChoice();
    }
    ForceNetUpdate();
    FinishReward(false);
    return true;
}
