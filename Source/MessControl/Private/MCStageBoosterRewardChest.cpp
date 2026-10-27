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
    if(!HasAuthority() || bStageRecipientsCaptured) return;
    for(TActorIterator<AMCPlayerState> It(GetWorld());It;++It) if(IsStageParticipant(*It)) {
        StageRecipients.Add(*It);
        BuildTeamBoosterOffer(It->Perks);
    }
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
            StageClaims.Remove(*It);StageOffers.Remove(*It);It.RemoveCurrent();
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
            BuildTeamBoosterOffer(State->Perks);
            const auto* Offer=StageOffers.Find(State);
            if(!Offer || Offer->IDs.IsEmpty()) {ResolveStageSkip(State);continue;}
            if(Cast<AMCPlaytestBotController>(Hero->GetController())) {
                bool Granted=false;
                for(FName ID:Offer->IDs) if(State->Perks->CanAcquirePerk(ID,INDEX_NONE)) {
                    Granted=ApplyStageRewardOnce(State,ID,INDEX_NONE,NAME_None,FGuid());break;
                }
                if(!Granted) ResolveStageSkip(State);
            }
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
    auto* State=Perks?Cast<AMCPlayerState>(Perks->GetOwner()):nullptr;
    if(!HasAuthority() || !State || Perks->GetPerkTable()!=RewardTable || !RewardTable
        || RewardTable->GetRowStruct()!=FMCPerkDefinition::StaticStruct()) return false;
    if(StageOffers.Contains(State)) return true;
    FMCPersonalRewardOffer Offer;Offer.OfferID=FGuid::NewGuid();
    const uint32 Identity=HashCombineFast(GetTypeHash(State->GetPlayerId()),GetTypeHash(State->GetFName()));
    const int32 Seed=int32(HashCombineFast(uint32(RollSeed)^0x534B494Cu,Identity));
    Perks->BuildRogueOffer(EMCPerkPolarity::Positive,Seed,Offer.IDs,false,false,false,TArray<FName>(),false);
    // An empty eligible pool is a real receipt too. It is resolved as a skip,
    // never repopulated with fixed fallback IDs or rerolled on reopen/death.
    StageOffers.Add(State,MoveTemp(Offer));Polarity=EMCPerkPolarity::Positive;
    return true;
}

bool AMCRewardChest::PublishStageChoiceFor(AMCToothCharacter* Player)
{
    if(!HasAuthority() || !bTeamStageBooster || bReported || Stage!=EMCRewardChestStage::Open
        || !CanReceiveStageChoice(Player)) return false;
    auto* State=Player->GetPlayerState<AMCPlayerState>();
    auto* PC=Cast<AMCPlayerController>(Player->GetController());
    if(!PC || !StageRecipients.Contains(State) || StageClaims.Contains(State)
        || !BuildTeamBoosterOffer(State->Perks)) return false;
    const auto* Offer=StageOffers.Find(State);
    if(!Offer || Offer->IDs.IsEmpty()) return ResolveStageSkip(State);
    Player->RewardInteraction=this;Player->ForceNetUpdate();
    StageChoicePawns.Add(State,Player);StageChoiceLastSent.Add(State,ServerNow());
    Player->CancelGameplayInput();
    PC->ClientShowPerkChoices(this,Offer->IDs,Polarity,true,GetStageChoiceDeadline(State),Offer->OfferID);
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
    if(Deadline<=0. || ServerNow()<Deadline) return false;
    const auto* Offer=StageOffers.Find(State);
    TArray<FName> Safe;
    if(State && State->Perks && Offer) for(FName ID:Offer->IDs)
        if(State->Perks->CanAcquirePerk(ID,INDEX_NONE)) Safe.Add(ID);
    if(Safe.IsEmpty()) return ResolveStageSkip(State);
    // Only an issued reward that fits without replacement is safe for AFK
    // resolution. Passive rewards remain available even with four full slots.
    FRandomStream Roll(int32(HashCombineFast(uint32(RollSeed)^0x4155544Fu,GetTypeHash(State->GetPlayerId()))));
    const FName ID=Safe[Roll.RandRange(0,Safe.Num()-1)];
    if(!ApplyStageRewardOnce(State,ID,INDEX_NONE,NAME_None,FGuid())) return ResolveStageSkip(State);
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_AUTO_CHOICE chest=%s player=%s skill=%s deadline=%.2f"),
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

bool AMCRewardChest::TryChooseTeamBooster(AMCToothCharacter* Player,int32 Index,int32 DestinationSlot,
    FName ExpectedReplacedID,FGuid ExpectedSlotInstance)
{
    if(!HasStageChoiceFor(Player) || bClaimInProgress || Stage!=EMCRewardChestStage::Open) return false;
    auto* State=Player->GetPlayerState<AMCPlayerState>();
    const auto* Offer=StageOffers.Find(State);
    if(!Offer || !Offer->IDs.IsValidIndex(Index)) return false;
    if(ServerNow()>=GetStageChoiceDeadline(State)) {
        const bool Resolved=ResolveExpiredStageChoice(State);
        if(AreStageChoicesResolved()) {ClaimedMask=7;ForceNetUpdate();FinishReward(false);}
        return Resolved;
    }
    const FName ID=Offer->IDs[Index];
    if(!ApplyStageRewardOnce(State,ID,DestinationSlot,ExpectedReplacedID,ExpectedSlotInstance)) return false;
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_CLAIM chest=%s player=%s skill=%s slot=%d resolved=%d/%d"),
        *GetName(),*State->GetPlayerName(),*ID.ToString(),DestinationSlot,StageClaims.Num(),StageRecipients.Num());
    if(AreStageChoicesResolved()) {ClaimedMask=7;ForceNetUpdate();FinishReward(false);}
    return true;
}

bool AMCRewardChest::ApplyStageRewardOnce(AMCPlayerState* State,FName ID,int32 DestinationSlot,
    FName ExpectedReplacedID,FGuid ExpectedSlotInstance)
{
    const auto* Offer=StageOffers.Find(State);
    if(!HasAuthority() || !bTeamStageBooster || bReported || bClaimInProgress || Stage!=EMCRewardChestStage::Open || !IsStageParticipant(State)
        || !StageRecipients.Contains(State) || StageClaims.Contains(State) || !State->Perks || !Offer
        || State->Perks->GetPerkTable()!=RewardTable || !Offer->IDs.Contains(ID)) return false;
    TGuardValue<bool> Guard(bClaimInProgress,true);
    StageClaims.Add(State);
    const bool Accepted=State->Perks->GetPerkKind(ID)==EMCPerkKind::ToolUpgrade?
        State->Perks->ServerGrantReward(ID):
        State->Perks->TryAcquirePerk(ID,DestinationSlot,ExpectedReplacedID,ExpectedSlotInstance);
    if(!Accepted) {StageClaims.Remove(State);return false;}
    ClaimedMask=1;State->RecordChestRewardChoice();ReleaseStageChoice(State);ForceNetUpdate();
    return true;
}

bool AMCRewardChest::ResolveStageSkip(AMCPlayerState* State)
{
    if(!HasAuthority() || !bTeamStageBooster || bReported || bClaimInProgress || Stage!=EMCRewardChestStage::Open || !State
        || !StageRecipients.Contains(State) || StageClaims.Contains(State)) return false;
    StageClaims.Add(State);ClaimedMask=1;
    ReleaseStageChoice(State);ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_STAGE_REWARD_SKIP chest=%s player=%s"),*GetName(),*State->GetPlayerName());
    return true;
}

TArray<FName> AMCRewardChest::GetStageOfferIDs(const AMCPlayerState* State) const
{
    if(const auto* Offer=StageOffers.Find(const_cast<AMCPlayerState*>(State))) return Offer->IDs;
    return TArray<FName>();
}

FGuid AMCRewardChest::GetChoiceOfferID(const AMCPlayerState* State) const
{
    if(!HasAuthority()) return FGuid();
    if(bTeamStageBooster) {
        if(const auto* Offer=StageOffers.Find(const_cast<AMCPlayerState*>(State))) return Offer->OfferID;
        return FGuid();
    }
    return IsValid(OpeningPlayer) && OpeningPlayer->GetPlayerState<AMCPlayerState>()==State?OpeningOfferID:FGuid();
}

bool AMCRewardChest::ValidateChoiceOffer(const AMCToothCharacter* Player,FGuid OfferID,FName ExpectedSkillID,int32 Index) const
{
    if(!HasAuthority() || !IsValid(Player) || Stage!=EMCRewardChestStage::Open || !OfferID.IsValid()
        || ExpectedSkillID.IsNone()) return false;
    const auto* State=Player->GetPlayerState<AMCPlayerState>();
    if(GetChoiceOfferID(State)!=OfferID) return false;
    if(bTeamStageBooster) {
        const auto* Offer=StageOffers.Find(const_cast<AMCPlayerState*>(State));
        return HasStageChoiceFor(Player) && Offer && Offer->IDs.IsValidIndex(Index) && Offer->IDs[Index]==ExpectedSkillID;
    }
    return OpeningPlayer==Player && OpenerCanContinue() && LootIDs.IsValidIndex(Index) && LootIDs[Index]==ExpectedSkillID;
}
