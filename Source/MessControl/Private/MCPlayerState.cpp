#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCHubEconomyComponent.h"
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
    Economy = CreateDefaultSubobject<UMCHubEconomyComponent>(TEXT("HubEconomy"));
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
void AMCPlayerState::AddCaughtFish()
{
    if(!HasAuthority()) return;
    FishCaught=int32(FMath::Min<int64>(int64(FishCaught)+1,MAX_int32));
    ForceNetUpdate();
}
void AMCPlayerState::CopyProperties(APlayerState* Target)
{
    Super::CopyProperties(Target);
    if (auto* State=Cast<AMCPlayerState>(Target))
    {
        State->Points=Points; State->PlayerColor=PlayerColor; State->bSessionHost=bSessionHost;
        State->FishCaught=FishCaught;
        if(Economy) Economy->CopyTo(State->Economy);
        State->bHubProfileLoaded=bHubProfileLoaded;
        State->ConsumedRecoveryStacks=ConsumedRecoveryStacks;
        if (Perks) Perks->CopyPerksTo(State->Perks);
        // Travel/reconnect preserves chest perks, never an obsolete level offer.
        State->PendingLevels.Reset(); State->LastQueuedTeamLevel=1;
        State->LevelUpOffer=FMCLevelUpOffer(); State->PendingLevelChoices=0;
        State->PersonalChoicesTaken=PersonalChoicesTaken;State->bRogueRerollSpent=bRogueRerollSpent;State->RecentRogueOffers=RecentRogueOffers;
    }
}
void AMCPlayerState::RecordChestRewardChoice()
{
    if (!HasAuthority()) return;
    PersonalChoicesTaken=FMath::Min(PersonalChoicesTaken,MAX_int32-1)+1;
    ForceNetUpdate();
}
void AMCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCPlayerState,Points); DOREPLIFETIME(AMCPlayerState,bSessionHost);
    DOREPLIFETIME(AMCPlayerState,FishCaught);
    DOREPLIFETIME(AMCPlayerState,PlayerColor); DOREPLIFETIME(AMCPlayerState,Alarm); DOREPLIFETIME(AMCPlayerState,AlarmUntil);
    DOREPLIFETIME_CONDITION(AMCPlayerState,LevelUpOffer,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMCPlayerState,PendingLevelChoices,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMCPlayerState,PersonalChoicesTaken,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMCPlayerState,bRogueRerollSpent,COND_OwnerOnly);
}

void AMCPlayerState::QueueChoicesThroughLevel(int32 TeamLevel)
{
    RefreshLevelUpOffer();
}

void AMCPlayerState::RefreshLevelUpOffer()
{
    if (!HasAuthority()) return;
    const bool HadOffer=!PendingLevels.IsEmpty() || PendingLevelChoices!=0 || LevelUpOffer.OfferId.IsValid();
    PendingLevels.Reset(); LastQueuedTeamLevel=1; PendingLevelChoices=0; LevelUpOffer=FMCLevelUpOffer();
    if (HadOffer) { ForceNetUpdate(); OnRep_LevelUpOffer(); }
}

bool AMCPlayerState::CreateRogueLevelOffer(bool Reroll)
{
    return false;
}

bool AMCPlayerState::TryRerollLevelPerks(const FGuid& OfferId)
{
    RefreshLevelUpOffer();
    return false;
}

bool AMCPlayerState::TryChooseLevelUpPerk(const FGuid& OfferId,int32 ChoiceIndex)
{
    RefreshLevelUpOffer();
    return false;
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
