#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
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
    }
}
void AMCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCPlayerState,Points); DOREPLIFETIME(AMCPlayerState,bSessionHost);
    DOREPLIFETIME(AMCPlayerState,PlayerColor); DOREPLIFETIME(AMCPlayerState,Alarm); DOREPLIFETIME(AMCPlayerState,AlarmUntil);
    DOREPLIFETIME_CONDITION(AMCPlayerState,LevelUpOffer,COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMCPlayerState,PendingLevelChoices,COND_OwnerOnly);
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
    auto* Table=Perks->GetPerkTable();
    TArray<FName> Pool;
    if (Table) for (FName ID:Table->GetRowNames())
    {
        const auto* Row=Perks->FindDefinition(ID);
        if (Row && Row->bAvailableForLevelChoice && Row->Polarity==EMCPerkPolarity::Positive && FMath::IsFinite(Row->Weight)
            && Row->Weight>0 && Perks->CanGrantPerk(ID)) Pool.Add(ID);
    }
    if (Pool.Num()<3)
    {
        if (!bWarnedEmptyLevelPool)
        {
            UE_LOG(LogTemp,Warning,TEXT("MC_LEVEL_WAIT player=%s needs three available positive perks; choice preserved"),*GetPlayerName());
            bWarnedEmptyLevelPool=true;
        }
        ForceNetUpdate(); return;
    }
    bWarnedEmptyLevelPool=false;
    Pool.Sort([](FName A,FName B){return A.LexicalLess(B);});
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    FRandomStream Random(int32(HashCombine(GetTypeHash(Game?Game->RunSeed:0),
        HashCombine(GetTypeHash(GetPlayerId()),GetTypeHash(PendingLevels[0])))));
    FMCLevelUpOffer Offer; Offer.OfferId=FGuid::NewGuid(); Offer.TeamLevel=PendingLevels[0];
    for (int32 Card=0;Card<3;++Card)
    {
        double Total=0; for (FName ID:Pool) Total+=Perks->FindDefinition(ID)->Weight;
        double Draw=Random.FRand()*Total; int32 Selected=Pool.Num()-1;
        for (int32 I=0;I<Pool.Num();++I) { Draw-=Perks->FindDefinition(Pool[I])->Weight; if (Draw<0) { Selected=I; break; } }
        Offer.PerkIDs.Add(Pool[Selected]); Pool.RemoveAt(Selected);
    }
    LevelUpOffer=MoveTemp(Offer); ForceNetUpdate(); OnRep_LevelUpOffer();
}

bool AMCPlayerState::TryChooseLevelUpPerk(const FGuid& OfferId,int32 ChoiceIndex)
{
    if (!HasAuthority() || bChoosingLevelPerk || !LevelUpOffer.IsValid() || LevelUpOffer.OfferId!=OfferId
        || !LevelUpOffer.PerkIDs.IsValidIndex(ChoiceIndex) || PendingLevels.IsEmpty()
        || PendingLevels[0]!=LevelUpOffer.TeamLevel || !Perks) return false;
    const FName ID=LevelUpOffer.PerkIDs[ChoiceIndex];
    const auto* Row=Perks->FindDefinition(ID);
    if (!Row || !Row->bAvailableForLevelChoice || Row->Polarity!=EMCPerkPolarity::Positive || !FMath::IsFinite(Row->Weight)
        || Row->Weight<=0 || !Perks->CanGrantPerk(ID)) { RefreshLevelUpOffer(); return false; }
    {
        TGuardValue<bool> Guard(bChoosingLevelPerk,true);
        // A personal level never calls the chest API that shares legendary tools.
        if (!Perks->ServerGrantPerk(ID)) return false;
        PendingLevels.RemoveAt(0); PendingLevelChoices=PendingLevels.Num(); LevelUpOffer=FMCLevelUpOffer();
    }
    UE_LOG(LogTemp,Display,TEXT("MC_LEVEL_CHOICE player=%s perk=%s remaining=%d"),*GetPlayerName(),*ID.ToString(),PendingLevelChoices);
    RefreshLevelUpOffer(); ForceNetUpdate(); OnRep_LevelUpOffer(); return true;
}

void AMCPlayerState::ResetLevelUpChoices()
{
    if (!HasAuthority()) return;
    PendingLevels.Reset(); LastQueuedTeamLevel=1; PendingLevelChoices=0; LevelUpOffer=FMCLevelUpOffer();
    bWarnedEmptyLevelPool=false; ForceNetUpdate(); OnRep_LevelUpOffer();
}

void AMCPlayerState::OnRep_LevelUpOffer()
{
    if (auto* PC=Cast<AMCPlayerController>(GetOwner())) PC->RefreshLevelPerkChoices();
}
