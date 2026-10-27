#include "MCRewardChest.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCPlaytestBotController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
bool IsStageParticipant(const AMCPlayerState* State)
{
    if(!IsValid(State) || State->IsActorBeingDestroyed() || State->IsInactive()
        || State->IsSpectator() || State->IsOnlyASpectator()) return false;
    const auto* Controller=Cast<AController>(State->GetOwner());
    return IsValid(Controller) && !Controller->IsActorBeingDestroyed()
        && Controller->GetPlayerState<AMCPlayerState>()==State
        && (Controller->IsA<AMCPlayerController>() || Controller->IsA<AMCPlaytestBotController>());
}
}

void AMCRewardChest::InitializeTeamStageReward(FVector Landing,FVector Start,int32 Seed,UDataTable* Table,AMCRewardDropZone* Zone)
{
    if(!HasAuthority()) return;
    bTeamStageBooster=true; bAlwaysRelevant=true; MimicChance=0; RogueKind=EMCRogueRewardKind::Ordinary;
    InitializeReward(Landing,Start,Seed,Table,EMCRewardSelectionPolicy::ChooseOne,Zone);
    Stage=EMCRewardChestStage::Falling;StageStartedAt=ServerNow();
    LastPresentationStage=EMCRewardChestStage::Falling;
    SetActorLocation(FallStart);
    if(HasActorBegunPlay()) RefreshPresentation();
    ForceNetUpdate();
}

void AMCRewardChest::CaptureStageRecipients()
{
    if(bStageRecipientsCaptured) return;
    for(TActorIterator<AMCPlayerState> It(GetWorld());It;++It)
        if(IsStageParticipant(*It)) StageRecipients.Add(*It);
    bStageRecipientsCaptured=!StageRecipients.IsEmpty();
}

bool AMCRewardChest::CanReceiveStageChoice(const AMCToothCharacter* Player) const
{
    if(!IsLivingPlayer(Player) || Player->GetWorld()!=GetWorld() || !Player->ToothPhysics->CanAct()
        || Player->HasFrozenLegs() || Player->IsMimicCaptured() || Player->SwallowedBy
        || !Player->Inventory || Player->Inventory->bChainsawRunning
        || (IsValid(Player->RewardInteraction) && Player->RewardInteraction!=this)) return false;
    const auto* State=Player->GetPlayerState<AMCPlayerState>();
    return IsStageParticipant(State) && State->Perks && State->Perks->GetPerkTable()==RewardTable
        && !State->Perks->IsFuryActive();
}

bool AMCRewardChest::HasStageChoiceFor(const AMCToothCharacter* Player) const
{
    auto* State=IsValid(Player)?Player->GetPlayerState<AMCPlayerState>():nullptr;
    return HasAuthority() && bTeamStageBooster && !bReported && State
        && (Stage==EMCRewardChestStage::Opening || Stage==EMCRewardChestStage::Open)
        && StageRecipients.Contains(State) && !StageClaims.Contains(State)
        && CanReceiveStageChoice(Player) && Player->RewardInteraction==this;
}

void AMCRewardChest::ReleaseStageChoice(AMCPlayerState* State)
{
    if(!State) return;
    if(auto* Choice=StageChoicePawns.Find(State)) {
        if(auto* Hero=Choice->Get();IsValid(Hero) && Hero->RewardInteraction==this) {
            Hero->RewardInteraction=nullptr;Hero->ForceNetUpdate();
        }
        StageChoicePawns.Remove(State);
    }
    StageChoicePresented.Remove(State);StageChoiceLastSent.Remove(State);
    // The controller survives the old pawn. Closing through it restores input
    // even when death already unpossessed the choice pawn.
    if(auto* PC=Cast<AMCPlayerController>(State->GetOwner());IsValid(PC) && !PC->IsActorBeingDestroyed())
        PC->ClientClosePerkChoices(this);
}

void AMCRewardChest::ReleaseStageChoices()
{
    TArray<TWeakObjectPtr<AMCPlayerState>> Players;
    StageChoicePawns.GetKeys(Players);
    for(const auto& Player:Players) if(auto* State=Player.Get()) ReleaseStageChoice(State);
    StageChoicePawns.Reset();
    StageChoicePresented.Reset();StageChoiceLastSent.Reset();
}

bool AMCRewardChest::AreStageChoicesResolved() const
{
    if(!bStageRecipientsCaptured) return false;
    for(const auto& Recipient:StageRecipients) if(!StageClaims.Contains(Recipient)) return false;
    return true;
}

void AMCRewardChest::PollTeamBoosterApproach()
{
    if(!HasAuthority() || !bTeamStageBooster || bReported) return;
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if(!Game || Game->bLobbyWaiting || Game->Phase==EMCShiftPhase::Lost || Game->Phase==EMCShiftPhase::Won) {
        if(Game) FinishReward(false);
        return;
    }
    // A temporarily dead pawn keeps its entitlement while a reserve exists.
    // With no reserves it can never be replaced, even if teammates survive:
    // do not leave those teammates waiting on an impossible choice.
    for(auto It=StageRecipients.CreateIterator();It;++It) {
        auto* State=It->Get();
        const auto* Hero=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
        const bool NoReplacement=IsValid(Hero) && Hero->Status && !Hero->Status->IsAlive()
            && Game->AvailableArenaTeeth()==0;
        if(!IsStageParticipant(State) || NoReplacement) {
            if(State) ReleaseStageChoice(State);
            StageClaims.Remove(*It);It.RemoveCurrent();
        }
    }
    TArray<TWeakObjectPtr<AMCPlayerState>> Showing;
    StageChoicePawns.GetKeys(Showing);
    for(const auto& Recipient:Showing) {
        const auto* State=Recipient.Get();
        const auto* Hero=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
        if(!State || StageChoicePawns.FindRef(Recipient).Get()!=Hero || !CanReceiveStageChoice(Hero))
            if(auto* MutableState=Recipient.Get()) ReleaseStageChoice(MutableState);
    }
    if(AreStageChoicesResolved()) {
        ClaimedMask=7;ForceNetUpdate();FinishReward(false);return;
    }
    if(Stage==EMCRewardChestStage::Opening) return;
    if(Stage==EMCRewardChestStage::Open) {
        for(const auto& Recipient:StageRecipients) {
            auto* State=Recipient.Get();
            if(!State || StageClaims.Contains(Recipient)) continue;
            if(ServerNow()>=GetStageChoiceDeadline(State)) {
                ResolveExpiredStageChoice(State);
                continue;
            }
            auto* Hero=Cast<AMCToothCharacter>(State->GetPawn());
            if(!CanReceiveStageChoice(Hero)) continue;
            if(Cast<AMCPlaytestBotController>(Hero->GetController())) ApplyStageBoosterOnce(State,LootIDs[0]);
            else if(!StageChoicePawns.Contains(Recipient)
                || (!StageChoicePresented.Contains(Recipient) && ServerNow()-StageChoiceLastSent.FindRef(Recipient)>=1.))
                PublishStageChoiceFor(Hero);
        }
        if(AreStageChoicesResolved()) {ClaimedMask=7;ForceNetUpdate();FinishReward(false);}
        return;
    }
    if(Stage!=EMCRewardChestStage::Landed) return;
    TArray<AMCToothCharacter*> Nearby;
    bool HasHumanParticipant=false;
    for(TActorIterator<AMCPlayerController> It(GetWorld());It;++It) {
        if(!IsStageParticipant(It->GetPlayerState<AMCPlayerState>())) continue;
        HasHumanParticipant=true;
        auto* Hero=Cast<AMCToothCharacter>(It->GetPawn());
        if(CanReceiveStageChoice(Hero) && Hero->CanWork() && CanReachLockpick(Hero)) Nearby.Add(Hero);
    }
    if(!HasHumanParticipant) {
        // Diagnostic bots have no modal UI. Each bot receives its own choice;
        // they never consume a human's pending reward.
        for(TActorIterator<AMCPlaytestBotController> It(GetWorld());It;++It) {
            auto* State=It->GetPlayerState<AMCPlayerState>();
            if(!IsStageParticipant(State) || !CanReceiveStageChoice(Cast<AMCToothCharacter>(It->GetPawn()))
                || !BuildTeamBoosterOffer(State->Perks)) continue;
            CaptureStageRecipients();SetStage(EMCRewardChestStage::Open);
            PollTeamBoosterApproach();break;
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
    if(!HasAuthority() || !bTeamStageBooster || bReported || Stage!=EMCRewardChestStage::Landed || bClaimInProgress
        || !CanReceiveStageChoice(Player) || !Player->CanWork() || !CanReachLockpick(Player)) return false;
    auto* PC=Cast<AMCPlayerController>(Player->GetController());
    auto* PS=Player->GetPlayerState<AMCPlayerState>();
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if(!PC || PC->IsRewardInteractionActive() || !Game || Game->bLobbyWaiting || Game->Phase==EMCShiftPhase::Lost
        || Game->Phase==EMCShiftPhase::Won || !BuildTeamBoosterOffer(PS->Perks)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCStageRewardApproach),true,this);Query.AddIgnoredActor(Player);
    FHitResult Block;
    if(GetWorld()->LineTraceSingleByChannel(Block,Player->GetActorLocation()+FVector(0,0,35),Solid->Bounds.Origin,ECC_Visibility,Query)) return false;
    CaptureStageRecipients();
    if(!StageRecipients.Contains(PS)) return false;
    OpeningPlayer=Player;Player->RewardInteraction=this;Player->ForceNetUpdate();
    StageChoicePawns.Add(PS,Player);
    SetStage(EMCRewardChestStage::Opening);
    Player->CancelGameplayInput();
    PC->ClientShowRewardOpening(this,StageStartedAt+FMath::Max(.1f,OpeningSeconds));
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_OPEN chest=%s player=%s recipients=%d"),*GetName(),*PS->GetPlayerName(),StageRecipients.Num());
    return true;
}

bool AMCRewardChest::BuildTeamBoosterOffer(const UMCPerkComponent* Perks)
{
    if(!Perks || Perks->GetPerkTable()!=RewardTable || !RewardTable
        || RewardTable->GetRowStruct()!=FMCPerkDefinition::StaticStruct()) return false;
    TArray<FName> Offers={TEXT("Fallback_Mouthwash"),TEXT("Fallback_BrushFury"),TEXT("Fallback_Grenade")};
    for(FName ID:Offers) {
        const auto* Row=RewardTable->FindRow<FMCPerkDefinition>(ID,TEXT("Personal stage booster"),false);
        if(!Row || !Row->bConsumableBooster || Row->Polarity!=EMCPerkPolarity::Positive) return false;
    }
    FRandomStream Roll(RollSeed);
    for(int32 I=Offers.Num()-1;I>0;--I) Offers.Swap(I,Roll.RandRange(0,I));
    LootIDs=MoveTemp(Offers);Polarity=EMCPerkPolarity::Positive;
    return true;
}

bool AMCRewardChest::PublishStageChoiceFor(AMCToothCharacter* Player)
{
    if(!HasAuthority() || !bTeamStageBooster || bReported || Stage!=EMCRewardChestStage::Open
        || LootIDs.Num()!=3 || !CanReceiveStageChoice(Player)) return false;
    auto* State=Player->GetPlayerState<AMCPlayerState>();
    auto* PC=Cast<AMCPlayerController>(Player->GetController());
    if(!PC || !StageRecipients.Contains(State) || StageClaims.Contains(State)) return false;
    Player->RewardInteraction=this;Player->ForceNetUpdate();
    StageChoicePawns.Add(State,Player);
    StageChoiceLastSent.Add(State,ServerNow());
    Player->CancelGameplayInput();
    PC->ClientShowPerkChoices(this,LootIDs,Polarity,true,GetStageChoiceDeadline(State));
    return true;
}

double AMCRewardChest::GetStageChoiceDeadline(const AMCPlayerState* State) const
{
    auto* Recipient=const_cast<AMCPlayerState*>(State);
    if(!HasAuthority() || !bTeamStageBooster || bReported || Stage!=EMCRewardChestStage::Open
        || !Recipient || !StageRecipients.Contains(Recipient) || StageClaims.Contains(Recipient)) return 0.;
    // Open is entered exactly once. Capturing the entire selection window here
    // also bounds players who cannot acknowledge/show their first modal yet.
    return StageStartedAt+40.;
}

bool AMCRewardChest::ResolveExpiredStageChoice(AMCPlayerState* State)
{
    const double Deadline=GetStageChoiceDeadline(State);
    if(Deadline<=0. || ServerNow()<Deadline || LootIDs.Num()!=3) return false;
    // Independent per-recipient draw from exactly the issued cards. A late
    // button RPC, repeated polling or replaced body cannot reroll this choice.
    FRandomStream Roll(int32(HashCombineFast(uint32(RollSeed)^0x4155544Fu,GetTypeHash(State->GetPlayerId()))));
    const FName ID=LootIDs[Roll.RandRange(0,LootIDs.Num()-1)];
    if(!ApplyStageBoosterOnce(State,ID,true)) return false;
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_AUTO_CHOICE chest=%s player=%s booster=%s deadline=%.2f"),
        *GetName(),*State->GetPlayerName(),*ID.ToString(),Deadline);
    return true;
}

bool AMCRewardChest::ConfirmStageChoicePresented(AMCToothCharacter* Player)
{
    if(Stage!=EMCRewardChestStage::Open || !HasStageChoiceFor(Player)) return false;
    auto* State=Player->GetPlayerState<AMCPlayerState>();
    if(StageChoicePawns.FindRef(State).Get()!=Player) return false;
    StageChoicePresented.Add(State);
    return true;
}

bool AMCRewardChest::TryChooseTeamBooster(AMCToothCharacter* Player,int32 Index)
{
    if(!HasStageChoiceFor(Player) || bClaimInProgress || Stage!=EMCRewardChestStage::Open
        || LootIDs.Num()!=3 || !LootIDs.IsValidIndex(Index)) return false;
    auto* State=Player->GetPlayerState<AMCPlayerState>();
    if(ServerNow()>=GetStageChoiceDeadline(State)) {
        const bool Resolved=ResolveExpiredStageChoice(State);
        if(AreStageChoicesResolved()) {ClaimedMask=7;ForceNetUpdate();FinishReward(false);}
        return Resolved;
    }
    const FName ID=LootIDs[Index];
    if(!ApplyStageBoosterOnce(State,ID)) return false;
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_CLAIM chest=%s player=%s booster=%s claimed=%d/%d"),
        *GetName(),*State->GetPlayerName(),*ID.ToString(),StageClaims.Num(),StageRecipients.Num());
    if(AreStageChoicesResolved()) {ClaimedMask=7;ForceNetUpdate();FinishReward(false);}
    return true;
}

bool AMCRewardChest::ApplyStageBoosterOnce(AMCPlayerState* State,FName ID,bool bAllowDeferred)
{
    if(!HasAuthority() || !bTeamStageBooster || bReported || bClaimInProgress || !IsStageParticipant(State)
        || !StageRecipients.Contains(State) || StageClaims.Contains(State) || !State->Perks
        || State->Perks->GetPerkTable()!=RewardTable || !LootIDs.Contains(ID) || !UMCPerkComponent::IsTeamStageBoosterID(ID)) return false;
    TGuardValue<bool> Guard(bClaimInProgress,true);
    // Seal by stable recipient before callbacks: repeated RPCs cannot replay an
    // effect, even after that player's pawn has been replaced.
    StageClaims.Add(State);
    const bool Accepted=bAllowDeferred?State->Perks->AcceptTimedStageBooster(ID):State->Perks->ActivatePersonalStageBooster(ID);
    if(!Accepted) {StageClaims.Remove(State);return false;}
    ClaimedMask=1; // An unexpected destruction must not reissue a real grant.
    State->RecordChestRewardChoice();
    ReleaseStageChoice(State);
    ForceNetUpdate();
    return true;
}
