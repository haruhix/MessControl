#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AMCPlayerState::AMCPlayerState()
{
    Perks = CreateDefaultSubobject<UMCPerkComponent>(TEXT("Perks"));
}

int32 FMCScoreRewards::ForTask(EMCScoreTask Task) const
{
    switch (Task)
    {
    case EMCScoreTask::Coffee: return FMath::Clamp(Coffee,0,100000);
    case EMCScoreTask::Repair: return FMath::Clamp(Repair,0,100000);
    case EMCScoreTask::Food: return FMath::Clamp(Food,0,100000);
    case EMCScoreTask::Ulcer: return FMath::Clamp(Ulcer,0,100000);
    case EMCScoreTask::Ice: return FMath::Clamp(Ice,0,100000);
    }
    return 0;
}
void AMCPlayerState::AddPoints(int32 Amount)
{
    if (!HasAuthority() || Amount<=0) return;
    Points=static_cast<int32>(FMath::Min<int64>(int64(Points)+Amount,MAX_int32));
    SetScore(float(Points)); ForceNetUpdate();
}
void AMCPlayerState::ResetMatchScore()
{
    if (!HasAuthority()) return;
    Points=0; SetScore(0); Alarm=EMCPlayerAlarm::None; AlarmUntil=0;
    if (Perks) Perks->ResetPerks();
    ConsumedRecoveryStacks.Reset();
    ResetLevelUpChoices();
    ForceNetUpdate();
}
void AMCPlayerState::CopyProperties(APlayerState* Target)
{
    Super::CopyProperties(Target);
    if (auto* State=Cast<AMCPlayerState>(Target))
    {
        State->Points=Points; State->PlayerColor=PlayerColor; State->bSessionHost=bSessionHost;
        State->ConsumedRecoveryStacks=ConsumedRecoveryStacks;
        if (Perks) Perks->CopyPerksTo(State->Perks);
        State->PendingLevels=PendingLevels; State->LastQueuedTeamLevel=LastQueuedTeamLevel;
        State->LevelUpOffer=LevelUpOffer; State->PendingLevelChoices=PendingLevelChoices;
        State->PersonalChoicesTaken=PersonalChoicesTaken;State->bRogueRerollSpent=bRogueRerollSpent;State->RecentRogueOffers=RecentRogueOffers;
    }
}
void AMCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCPlayerState,Points); DOREPLIFETIME(AMCPlayerState,bSessionHost);
    DOREPLIFETIME(AMCPlayerState,PlayerColor); DOREPLIFETIME(AMCPlayerState,Alarm); DOREPLIFETIME(AMCPlayerState,AlarmUntil);
    DOREPLIFETIME_CONDITION(AMCPlayerState,LevelUpOffer,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMCPlayerState,PendingLevelChoices,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMCPlayerState,PersonalChoicesTaken,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMCPlayerState,bRogueRerollSpent,COND_OwnerOnly);
}

void AMCPlayerState::QueueChoicesThroughLevel(int32 TeamLevel)
{
    if (!HasAuthority()) return;
    const int32 Target=FMath::Clamp(TeamLevel,1,1000);
    const bool Added=Target>LastQueuedTeamLevel;
    for (int32 Level=LastQueuedTeamLevel+1;Level<=Target;++Level) PendingLevels.Add(Level);
    LastQueuedTeamLevel=FMath::Max(LastQueuedTeamLevel,Target);
    PendingLevelChoices=PendingLevels.Num();
    if (Added) ForceNetUpdate();
    RefreshLevelUpOffer();
}

void AMCPlayerState::RefreshLevelUpOffer()
{
    if (!HasAuthority() || !Perks || PendingLevels.IsEmpty() || bChoosingLevelPerk) return;
    if (LevelUpOffer.IsValid())
    {
        bool Usable=true;
        for (FName ID:LevelUpOffer.PerkIDs) Usable&=Perks->CanGrantPerk(ID);
        if (Usable) return;
        LevelUpOffer=FMCLevelUpOffer();
    }
    CreateRogueLevelOffer(false);
}

bool AMCPlayerState::CreateRogueLevelOffer(bool Reroll)
{
    if(!HasAuthority() || !Perks || PendingLevels.IsEmpty()) return false;
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    const int32 Seed=int32(HashCombine(GetTypeHash(Game?Game->RunSeed:0),HashCombine(GetTypeHash(GetPlayerId()),GetTypeHash(PendingLevels[0]))))^(Reroll?0x5245524F:0);
    TArray<FName> Excluded;for(const auto& Recent:RecentRogueOffers) Excluded.Append(Recent.IDs);
    if(Reroll) Excluded.Append(LevelUpOffer.PerkIDs);
    FMCLevelUpOffer Offer;Offer.OfferId=FGuid::NewGuid();Offer.TeamLevel=PendingLevels[0];
    const bool Early=PersonalChoicesTaken<2,GuaranteedRare=(PersonalChoicesTaken+1)%5==0;
    if(!Perks->BuildRogueOffer(EMCPerkPolarity::Positive,Seed,Offer.PerkIDs,Early,false,GuaranteedRare,Excluded)) {
        // Consumable charges remain useful after every permanent branch reaches its cap.
        for(FName ID:{FName(TEXT("Fallback_Mouthwash")),FName(TEXT("Fallback_BrushFury")),FName(TEXT("Fallback_Grenade"))})
            if(Perks->CanGrantPerk(ID)) Offer.PerkIDs.Add(ID);
        Offer.bBoosterFallback=true;
        if(Offer.PerkIDs.Num()!=3) {if(!bWarnedEmptyLevelPool) UE_LOG(LogTemp,Warning,TEXT("MC_ROGUE_WAIT insufficient eligible cards; earned level preserved"));bWarnedEmptyLevelPool=true;return false;}
    }
    if(!Early && !Offer.bBoosterFallback) {
        FRandomStream Random(Seed^0x5249534B);TArray<FName> Risky;
        if(Random.FRand()<.25f && Perks->BuildRogueOffer(EMCPerkPolarity::Risky,Seed^0x5249534B,Risky,false,false,false,Excluded)) {
            for(FName ID:Risky) {
                const auto* Candidate=Perks->FindDefinition(ID);bool Valid=true;
                for(int32 I=0;I<2;++I) {
                    const auto* Existing=Perks->FindDefinition(Offer.PerkIDs[I]);
                    if(Existing->ChoiceFamily==Candidate->ChoiceFamily || (Existing->Rarity==EMCPerkRarity::Legendary && Candidate->Rarity==EMCPerkRarity::Legendary)) Valid=false;
                }
                if(Valid) {Offer.PerkIDs[2]=ID;break;}
            }
        }
    }
    if(Reroll && !Offer.PerkIDs.ContainsByPredicate([this](FName ID){return !LevelUpOffer.PerkIDs.Contains(ID);})) return false;
    if(Reroll) bRogueRerollSpent=true;
    bWarnedEmptyLevelPool=false;LevelUpOffer=MoveTemp(Offer);ForceNetUpdate();OnRep_LevelUpOffer();return true;
}
bool AMCPlayerState::TryRerollLevelPerks(const FGuid& OfferId)
{
    const auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!HasAuthority() || bRogueRerollSpent || bChoosingLevelPerk || !LevelUpOffer.IsValid() || OfferId!=LevelUpOffer.OfferId
        || !Hero || !Hero->Status->IsAlive()) return false;
    return CreateRogueLevelOffer(true);
}

bool AMCPlayerState::TryChooseLevelUpPerk(const FGuid& OfferId,int32 ChoiceIndex)
{
    if (!HasAuthority() || bChoosingLevelPerk || !LevelUpOffer.IsValid() || LevelUpOffer.OfferId!=OfferId
        || !LevelUpOffer.PerkIDs.IsValidIndex(ChoiceIndex) || PendingLevels.IsEmpty()
        || PendingLevels[0]!=LevelUpOffer.TeamLevel || !Perks) return false;
    const FName ID=LevelUpOffer.PerkIDs[ChoiceIndex];
    const auto* Row=Perks->FindDefinition(ID);
    if (!Row || !Row->bAvailableForLevelChoice || Row->Polarity==EMCPerkPolarity::Negative || !FMath::IsFinite(Row->Weight)
        || Row->Weight<=0 || !Perks->CanGrantPerk(ID)) { RefreshLevelUpOffer(); return false; }
    {
        TGuardValue<bool> Guard(bChoosingLevelPerk,true);
        // A personal level never calls the chest API that shares legendary tools.
        if (!Perks->ServerGrantPerk(ID)) return false;
        FMCRogueRecentOffer Recent;Recent.IDs=LevelUpOffer.PerkIDs;Recent.IDs.Remove(ID);RecentRogueOffers.Add(Recent);
        if(RecentRogueOffers.Num()>2) RecentRogueOffers.RemoveAt(0);
        ++PersonalChoicesTaken;
        PendingLevels.RemoveAt(0); PendingLevelChoices=PendingLevels.Num(); LevelUpOffer=FMCLevelUpOffer();
    }
    UE_LOG(LogTemp,Display,TEXT("MC_LEVEL_CHOICE player=%s perk=%s remaining=%d"),*GetPlayerName(),*ID.ToString(),PendingLevelChoices);
    RefreshLevelUpOffer(); ForceNetUpdate(); OnRep_LevelUpOffer(); return true;
}

void AMCPlayerState::ResetLevelUpChoices()
{
    if (!HasAuthority()) return;
    PendingLevels.Reset(); LastQueuedTeamLevel=1; PendingLevelChoices=0; LevelUpOffer=FMCLevelUpOffer();
    PersonalChoicesTaken=0;bRogueRerollSpent=false;RecentRogueOffers.Reset();
    bWarnedEmptyLevelPool=false; ForceNetUpdate(); OnRep_LevelUpOffer();
}

void AMCPlayerState::OnRep_LevelUpOffer()
{
    if (auto* PC=Cast<AMCPlayerController>(GetOwner())) PC->RefreshLevelPerkChoices();
}
