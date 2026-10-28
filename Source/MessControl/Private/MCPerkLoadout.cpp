#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace
{
constexpr int32 MaximumSkillCharges=10000;
FName ActionID(FName ID,const FMCPerkDefinition& Row)
{ return Row.ActivationAdapterID.IsNone()?ID:Row.ActivationAdapterID; }
bool IsRecovery(FName ID)
{ return ID==TEXT("RecoverySelfHeal") || ID==TEXT("RecoveryMouthHeal") || ID==TEXT("RecoveryCleanse"); }
}

EMCPerkKind UMCPerkComponent::ResolvePerkKind(const FMCPerkDefinition& Row,FName ID)
{
    if(Row.Kind!=EMCPerkKind::Automatic) return Row.Kind;
    if(Row.ToolUpgrade!=EMCToolUpgrade::None) return EMCPerkKind::ToolUpgrade;
    if(Row.bConsumableBooster || IsRecovery(ID)) return EMCPerkKind::Consumable;
    return EMCPerkKind::Passive;
}
EMCPerkKind UMCPerkComponent::GetPerkKind(FName ID) const
{ const auto* Row=FindDefinition(ID);return Row?ResolvePerkKind(*Row,ID):EMCPerkKind::Automatic; }
bool UMCPerkComponent::OwnsPerk(FName ID) const
{
    if(ID.IsNone()) return false;
    return PassivePerkIDs.Contains(ID) || OwnedToolPerkIDs.Contains(ID)
        || ActiveSkillSlots.ContainsByPredicate([ID](const FMCActiveSkillSlot& Slot){return Slot.PerkID==ID;});
}
int32 UMCPerkComponent::GetStacks(FName ID) const
{ const auto* Row=FindDefinition(ID);return Row && IsDefinitionUsable(*Row) && OwnsPerk(ID)?1:0; }
int32 UMCPerkComponent::FindFreeActiveSlot() const
{
    for(int32 I=0;I<ActiveSkillSlotCount;++I)
        if(ActiveSkillSlots.IsValidIndex(I) && ActiveSkillSlots[I].PerkID.IsNone()) return I;
    return INDEX_NONE;
}
bool UMCPerkComponent::CanOfferPerk(FName ID) const
{
    const auto* Row=FindDefinition(ID);
    if(!Row || !IsDefinitionUsable(*Row) || OwnsPerk(ID)) return false;
    const EMCPerkKind Kind=ResolvePerkKind(*Row,ID);
    if(Kind==EMCPerkKind::Automatic || uint8(Kind)>uint8(EMCPerkKind::ToolUpgrade)) return false;
    if((Kind==EMCPerkKind::ToolUpgrade)!=(Row->ToolUpgrade!=EMCToolUpgrade::None)) return false;
    if(Kind==EMCPerkKind::Consumable && (Row->InitialCharges<1 || Row->InitialCharges>MaximumSkillCharges)) return false;
    if(Kind==EMCPerkKind::ToolUpgrade) {
        const auto* Game=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
        if(Game && (Game->TeamToolUpgrades&(1<<(uint8(Row->ToolUpgrade)-1)))) return false;
        if(Row->Rarity==EMCPerkRarity::Rare && HasToolUpgrade(Row->ToolUpgrade)) return false;
    }
    return IsRogueEligible(ID);
}
bool UMCPerkComponent::CanAcquirePerk(FName ID,int32 Destination) const
{
    FMCPerkGrant Grant;Grant.PerkID=ID;Grant.DestinationSlot=Destination;
    if(ActiveSkillSlots.IsValidIndex(Destination) && !ActiveSkillSlots[Destination].PerkID.IsNone()) {
        Grant.ExpectedReplacedID=ActiveSkillSlots[Destination].PerkID;
        Grant.ExpectedInstanceID=ActiveSkillSlots[Destination].InstanceID;
    }
    return CanAcquirePerkBatch({Grant});
}
bool UMCPerkComponent::CanGrantPerk(FName ID) const {return CanAcquirePerk(ID);}
bool UMCPerkComponent::TryAcquirePerk(FName ID,int32 Destination,FName ExpectedID,FGuid ExpectedInstance)
{
    FMCPerkGrant Grant;Grant.PerkID=ID;Grant.DestinationSlot=Destination;
    Grant.ExpectedReplacedID=ExpectedID;Grant.ExpectedInstanceID=ExpectedInstance;
    return TryAcquirePerkBatch({Grant});
}
bool UMCPerkComponent::BuildAcquisitionPlan(const TArray<FMCPerkGrant>& Grants,TArray<FMCActiveSkillSlot>& Slots,TArray<FName>& Passives,TArray<FName>& Tools) const
{
    if(Grants.IsEmpty() || Grants.Num()>4096 || ActiveSkillSlots.Num()!=ActiveSkillSlotCount) return false;
    Slots=ActiveSkillSlots;Passives=PassivePerkIDs;Tools=OwnedToolPerkIDs;
    TSet<FName> IDs;TSet<int32> Destinations;
    // Resolve tool ownership before checking a modifier which requires that tool.
    for(const auto& Grant:Grants) {
        const auto* Row=FindDefinition(Grant.PerkID);
        if(!Row || IDs.Contains(Grant.PerkID) || OwnsPerk(Grant.PerkID) || !IsDefinitionUsable(*Row)) return false;
        IDs.Add(Grant.PerkID);
        if(GetPerkKind(Grant.PerkID)==EMCPerkKind::ToolUpgrade) {
            if(Grant.DestinationSlot!=INDEX_NONE || !Grant.ExpectedReplacedID.IsNone() || Grant.ExpectedInstanceID.IsValid()
                || Grant.Charges!=-1 || !CanOfferPerk(Grant.PerkID)) return false;
            Tools.AddUnique(Grant.PerkID);
        }
    }
    TGuardValue<const TArray<FName>*> ToolContext(ValidationToolPerks,&Tools);
    for(const auto& Grant:Grants) {
        const auto* Row=FindDefinition(Grant.PerkID);
        const EMCPerkKind Kind=ResolvePerkKind(*Row,Grant.PerkID);
        if(Kind==EMCPerkKind::ToolUpgrade) continue;
        if(!CanOfferPerk(Grant.PerkID)) return false;
        if(Kind==EMCPerkKind::Passive) {
            if(Grant.DestinationSlot!=INDEX_NONE || !Grant.ExpectedReplacedID.IsNone() || Grant.ExpectedInstanceID.IsValid() || Grant.Charges!=-1) return false;
            Passives.AddUnique(Grant.PerkID);continue;
        }
        int32 Destination=Grant.DestinationSlot;
        if(Destination==INDEX_NONE) {
            if(!Grant.ExpectedReplacedID.IsNone() || Grant.ExpectedInstanceID.IsValid()) return false;
            Destination=Slots.IndexOfByPredicate([](const FMCActiveSkillSlot& Slot){return Slot.PerkID.IsNone();});
        }
        if(!Slots.IsValidIndex(Destination) || Destinations.Contains(Destination)) return false;
        const auto& Previous=Slots[Destination];
        if(!Previous.PerkID.IsNone()) {
            if(Grant.ExpectedReplacedID!=Previous.PerkID || !Grant.ExpectedInstanceID.IsValid()
                || Grant.ExpectedInstanceID!=Previous.InstanceID) return false;
        } else if(!Grant.ExpectedReplacedID.IsNone() || Grant.ExpectedInstanceID.IsValid()) return false;
        int32 Charges=-1;
        if(Kind==EMCPerkKind::Consumable) {
            Charges=Grant.Charges==-1?Row->InitialCharges:Grant.Charges;
            if(Charges<1 || Charges>MaximumSkillCharges) return false;
        } else if(Kind!=EMCPerkKind::Active || Grant.Charges!=-1) return false;
        FMCActiveSkillSlot NewSlot;NewSlot.PerkID=Grant.PerkID;NewSlot.Charges=Charges;
        Slots[Destination]=NewSlot;Destinations.Add(Destination);
    }
    return true;
}
bool UMCPerkComponent::CanAcquirePerkBatch(const TArray<FMCPerkGrant>& Grants) const
{
    TArray<FMCActiveSkillSlot> Slots;TArray<FName> Passives,Tools;
    return !bRebuilding && !bSkillMutation && BuildAcquisitionPlan(Grants,Slots,Passives,Tools);
}
bool UMCPerkComponent::TryAcquirePerkBatch(const TArray<FMCPerkGrant>& Grants)
{
    if(!HasAuthority() || bRebuilding || bSkillMutation) return false;
    LoadDefinitions();
    TArray<FMCActiveSkillSlot> Slots;TArray<FName> Passives,Tools;
    if(!BuildAcquisitionPlan(Grants,Slots,Passives,Tools)) return false;
    TGuardValue<bool> Guard(bSkillMutation,true);
    for(auto& Slot:Slots) if(!Slot.PerkID.IsNone() && !Slot.InstanceID.IsValid()) Slot.InstanceID=FGuid::NewGuid();
    ActiveSkillSlots=MoveTemp(Slots);PassivePerkIDs=MoveTemp(Passives);OwnedToolPerkIDs=MoveTemp(Tools);
    RefreshLegacyProjection();RebuildEffects();GetOwner()->ForceNetUpdate();return true;
}
bool UMCPerkComponent::ServerGrantPerk(FName ID) {return TryAcquirePerk(ID);}
bool UMCPerkComponent::ServerGrantPerks(const TArray<FName>& IDs)
{
    if(!HasAuthority() || IDs.IsEmpty() || IDs.Num()>4096) return false;
    LoadDefinitions();TArray<FMCPerkGrant> Grants;
    for(FName ID:IDs) {
        auto* Existing=Grants.FindByPredicate([ID](const FMCPerkGrant& G){return G.PerkID==ID;});
        if(Existing) {
            if(GetPerkKind(ID)!=EMCPerkKind::Consumable) return false;
            // This is the legacy queue adapter: each old entry represented ONE charge.
            if(Existing->Charges==-1) Existing->Charges=1;
            ++Existing->Charges;
        } else {FMCPerkGrant Grant;Grant.PerkID=ID;if(GetPerkKind(ID)==EMCPerkKind::Consumable) Grant.Charges=1;Grants.Add(Grant);}
    }
    return TryAcquirePerkBatch(Grants);
}
void UMCPerkComponent::RefreshLegacyProjection()
{
    // Disabled entries remain explicit inert migration data, never live HUD/effects.
    bool Pruned=false;
    const auto Disabled=[&](FName ID) {
        const auto* Row=FindDefinition(ID);
        if(Row && IsDefinitionUsable(*Row)) return false;
        if(!ID.IsNone()) LegacyUnappliedBoosterIDs.AddUnique(ID);
        Pruned=true;return true;
    };
    PassivePerkIDs.RemoveAll(Disabled);OwnedToolPerkIDs.RemoveAll(Disabled);
    for(auto& Slot:ActiveSkillSlots) if(!Slot.PerkID.IsNone()) {
        const auto* Row=FindDefinition(Slot.PerkID);
        if(Row && IsDefinitionUsable(*Row)) continue;
        const int32 Units=Row && ResolvePerkKind(*Row,Slot.PerkID)==EMCPerkKind::Consumable?FMath::Clamp(Slot.Charges,1,MaximumSkillCharges):1;
        for(int32 I=0;I<Units;++I) LegacyUnappliedBoosterIDs.Add(Slot.PerkID);
        Slot=FMCActiveSkillSlot();Pruned=true;
    }
    if(Pruned) ClearRogueRuntime();
    ActivePerks.Reset();
    for(FName ID:PassivePerkIDs) {FMCActivePerk P;P.PerkID=ID;P.Stacks=1;ActivePerks.Add(P);}
    for(FName ID:OwnedToolPerkIDs) {FMCActivePerk P;P.PerkID=ID;P.Stacks=1;ActivePerks.Add(P);}
    ActivePerks.Sort([](const FMCActivePerk& A,const FMCActivePerk& B){return A.PerkID.LexicalLess(B.PerkID);});
    BoosterQueue.Reset();
    for(const auto& Slot:ActiveSkillSlots) if(!Slot.PerkID.IsNone()) {
        const int32 Units=GetPerkKind(Slot.PerkID)==EMCPerkKind::Consumable?Slot.Charges:1;
        for(int32 I=0;I<FMath::Clamp(Units,0,MaximumSkillCharges);++I) BoosterQueue.Add(Slot.PerkID);
        const auto* Row=FindDefinition(Slot.PerkID);if(!Row) continue;
        UClass* Class=Row->EffectClass?Row->EffectClass.Get():UMCPerkEffect::StaticClass();
        auto& Effect=Effects.FindOrAdd(Slot.PerkID);
        if(!Effect || Effect->GetClass()!=Class) {
            if(Effect) {Effect->CancelActivation();Effect->OnRemoved(Slot.PerkID);}
            Effect=NewObject<UMCPerkEffect>(this,Class);Effect->OwnerComponent=this;
        }
        // Active instances intentionally never receive the passive OnApplied callback.
    }
}
void UMCPerkComponent::MigrateLegacyOwnedState()
{
    if(!HasAuthority()) return;
    if(ActiveSkillSlots.Num()!=ActiveSkillSlotCount) ActiveSkillSlots.SetNum(ActiveSkillSlotCount);
    if(!PassivePerkIDs.IsEmpty() || !OwnedToolPerkIDs.IsEmpty()
        || ActiveSkillSlots.ContainsByPredicate([](const FMCActiveSkillSlot& S){return !S.PerkID.IsNone();})) return;
    const auto OldPerks=ActivePerks;const auto OldQueue=BoosterQueue;
    for(const auto& P:OldPerks) if(P.Stacks>0) {
        const auto Kind=GetPerkKind(P.PerkID);
        if(Kind==EMCPerkKind::Passive) PassivePerkIDs.AddUnique(P.PerkID);
        else if(Kind==EMCPerkKind::ToolUpgrade) OwnedToolPerkIDs.AddUnique(P.PerkID);
        else if(Kind==EMCPerkKind::Automatic) LegacyUnappliedBoosterIDs.AddUnique(P.PerkID);
    }
    TArray<FName> Order;TMap<FName,int32> Counts;
    for(FName ID:OldQueue) {if(!Counts.Contains(ID)) Order.Add(ID);++Counts.FindOrAdd(ID);}
    for(FName ID:Order) {
        const int32 Slot=FindFreeActiveSlot();
        if(GetPerkKind(ID)!=EMCPerkKind::Consumable || Slot==INDEX_NONE || !FindDefinition(ID)) {
            for(int32 I=0;I<Counts[ID];++I) LegacyUnappliedBoosterIDs.Add(ID);continue;
        }
        auto& S=ActiveSkillSlots[Slot];S.PerkID=ID;S.InstanceID=FGuid::NewGuid();S.Charges=Counts[ID];
    }
}
bool UMCPerkComponent::RemovePerk(FName ID)
{
    if(!HasAuthority() || bRebuilding || bSkillMutation || !OwnsPerk(ID)) return false;
    TGuardValue<bool> Guard(bSkillMutation,true);
    if(auto* Effect=Effects.Find(ID);Effect && *Effect) (*Effect)->CancelActivation();
    PassivePerkIDs.Remove(ID);OwnedToolPerkIDs.Remove(ID);
    for(auto& Slot:ActiveSkillSlots) if(Slot.PerkID==ID) Slot=FMCActiveSkillSlot();
    RefreshLegacyProjection();RebuildEffects();GetOwner()->ForceNetUpdate();return true;
}
void UMCPerkComponent::CancelSkillPreparations()
{
    for(const auto& Slot:ActiveSkillSlots) if(const auto* Effect=Effects.Find(Slot.PerkID);Effect && *Effect) (*Effect)->CancelActivation();
}
void UMCPerkComponent::ResetPerks()
{
    if(!HasAuthority() || bRebuilding || bSkillMutation) return;
    TGuardValue<bool> Guard(bSkillMutation,true);CancelSkillPreparations();
    ActiveSkillSlots.Reset();ActiveSkillSlots.SetNum(ActiveSkillSlotCount);PassivePerkIDs.Reset();OwnedToolPerkIDs.Reset();LegacyUnappliedBoosterIDs.Reset();
    ClearRogueRuntime();RefreshLegacyProjection();RebuildEffects();GetOwner()->ForceNetUpdate();
}
double UMCPerkComponent::SkillNow() const
{
    const auto* World=GetWorld();return World?(World->GetGameState()?World->GetGameState()->GetServerWorldTimeSeconds():World->GetTimeSeconds()):0.;
}
float UMCPerkComponent::GetSkillCooldownRemaining(int32 Index) const
{
    return ActiveSkillSlots.IsValidIndex(Index) && !ActiveSkillSlots[Index].PerkID.IsNone()
        ? float(FMath::Max(0.,ActiveSkillSlots[Index].CooldownEndsAt-SkillNow())):0.f;
}
bool UMCPerkComponent::CanUseSkillActor() const
{
    const auto* State=Cast<AMCPlayerState>(GetOwner());const auto* Hero=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
    const auto* PC=Hero?Cast<AMCPlayerController>(Hero->GetController()):nullptr;
    const auto* Game=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    return State && !State->IsInactive() && !State->IsSpectator() && !State->IsOnlyASpectator()
        && IsValid(Hero) && IsValid(PC) && State->GetOwner()==PC && PC->GetPlayerState<AMCPlayerState>()==State
        && PC->GetPawn()==Hero && Hero->GetPlayerState<AMCPlayerState>()==State && Hero->Status && Hero->Status->IsAlive()
        && Hero->CanWork() && !Hero->HasFrozenLegs() && !Hero->IsArenaArrivalHeld() && !Hero->RewardInteraction && !PC->IsRewardMenuOpen()
        && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored() && !PC->bShowMouseCursor
        && GetWorld() && !GetWorld()->IsPaused() && Game && !Game->bLobbyWaiting
        && Game->Phase==EMCShiftPhase::Working && !Game->bTutorialActive;
}
bool UMCPerkComponent::CanExecuteSkill(FName ID,const FMCPerkDefinition& Row) const
{
    const FName Action=ActionID(ID,Row);const auto* State=Cast<AMCPlayerState>(GetOwner());
    const auto* Hero=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
    const auto* Game=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!Hero || !Hero->Status) return false;
    if(Action==TEXT("RecoverySelfHeal")) return Hero->Status->State.Health<Hero->Status->State.MaxHealth;
    if(Action==TEXT("RecoveryMouthHeal")) return Game && Game->MouthHealth<Game->RunSettings.MaxMouthHealth;
    if(Action==TEXT("RecoveryCleanse")) return Hero->Status->State.CoffeeLeft>0 || Hero->Status->State.RepairLeft>0;
    if(IsTeamStageBoosterID(Action)) return !IsFuryActive() && (Action!=TEXT("Fallback_Grenade") || !bGrenadeArmed);
    const auto* Effect=Effects.Find(ID);return Effect && *Effect && (*Effect)->CanActivate(ID,Row);
}
bool UMCPerkComponent::ExecuteSkill(FName ID,const FMCPerkDefinition& Row)
{
    const FName Action=ActionID(ID,Row);
    if(IsTeamStageBoosterID(Action)) return ExecuteLegacyBooster(Action);
    auto* State=Cast<AMCPlayerState>(GetOwner());auto* Hero=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
    if(IsRecovery(Action)) {
        if(!CanExecuteSkill(ID,Row) || !Hero || !Hero->Status) return false;
        if(Action==TEXT("RecoveryMouthHeal")) {
            auto* Game=GetWorld()->GetGameState<AMCGameState>();
            Game->MouthHealth=FMath::Min(Game->RunSettings.MaxMouthHealth,Game->MouthHealth+10.f);Game->ForceNetUpdate();
        } else {
            FMCToothStatus Status=Hero->Status->State;
            if(Action==TEXT("RecoverySelfHeal")) Status.Health=FMath::Min(Status.MaxHealth,Status.Health+25.f);
            else {Status.CoffeeLeft=0;Status.RepairLeft=0;}
            Hero->Status->Restore(Status);
        }
        return true;
    }
    auto* Effect=Effects.Find(ID);return Effect && *Effect && (*Effect)->OnActivate(ID,Row);
}
bool UMCPerkComponent::ActivateSkillSlot(int32 Index,FGuid ExpectedInstance)
{
    if(!HasAuthority() || bRebuilding || bSkillMutation || !ActiveSkillSlots.IsValidIndex(Index) || !CanUseSkillActor()) return false;
    const auto Before=ActiveSkillSlots[Index];const auto* Row=FindDefinition(Before.PerkID);
    if(!Row || !Before.InstanceID.IsValid() || (ExpectedInstance.IsValid() && Before.InstanceID!=ExpectedInstance)
        || GetSkillCooldownRemaining(Index)>0 || !IsDefinitionUsable(*Row)) return false;
    const auto Kind=ResolvePerkKind(*Row,Before.PerkID);
    if((Kind!=EMCPerkKind::Active && Kind!=EMCPerkKind::Consumable)
        || (Kind==EMCPerkKind::Consumable && Before.Charges<=0) || !CanExecuteSkill(Before.PerkID,*Row)) return false;
    TGuardValue<bool> Guard(bSkillMutation,true);
    auto& Slot=ActiveSkillSlots[Index];
    if(Kind==EMCPerkKind::Consumable) --Slot.Charges;
    Slot.CooldownEndsAt=SkillNow()+Row->CooldownSeconds;
    if(!ExecuteSkill(Before.PerkID,*Row)) {Slot=Before;return false;}
    if(Kind==EMCPerkKind::Consumable && Slot.Charges==0) Slot=FMCActiveSkillSlot();
    RefreshLegacyProjection();RebuildEffects();GetOwner()->ForceNetUpdate();return true;
}
void UMCPerkComponent::CopyPerksTo(UMCPerkComponent* Target) const
{
    if(!HasAuthority() || !Target || !Target->HasAuthority() || Target==this || Target->bRebuilding || Target->bSkillMutation) return;
    Target->CancelSkillPreparations();Target->PerkTable=PerkTable;Target->LoadedTable=LoadedTable;Target->bDefinitionsLoaded=bDefinitionsLoaded;
    Target->ActiveSkillSlots=ActiveSkillSlots;Target->PassivePerkIDs=PassivePerkIDs;Target->OwnedToolPerkIDs=OwnedToolPerkIDs;
    Target->LegacyUnappliedBoosterIDs=LegacyUnappliedBoosterIDs;
    for(auto& Slot:Target->ActiveSkillSlots) if(!Slot.PerkID.IsNone()) Slot.CooldownEndsAt=Target->SkillNow()+FMath::Max(0.,Slot.CooldownEndsAt-SkillNow());
    // Old runtime snapshots retain unconsumed queue entries, never their already-used stack count.
    if(PassivePerkIDs.IsEmpty() && OwnedToolPerkIDs.IsEmpty()
        && !ActiveSkillSlots.ContainsByPredicate([](const FMCActiveSkillSlot& S){return !S.PerkID.IsNone();})) {
        Target->ActivePerks=ActivePerks;Target->BoosterQueue=BoosterQueue;Target->MigrateLegacyOwnedState();
    }
    bool GrenadeEnabled=false;
    if(LoadedTable) for(FName ID:LoadedTable->GetRowNames()) {
        const auto* Row=FindDefinition(ID);
        if(Row && IsDefinitionUsable(*Row) && ActionID(ID,*Row)==TEXT("Fallback_Grenade")) {GrenadeEnabled=true;break;}
    }
    Target->bGrenadeArmed=bGrenadeArmed && GrenadeEnabled;Target->DeliveryXPResidual=DeliveryXPResidual;
    Target->RefreshLegacyProjection();Target->RebuildEffects();Target->GetOwner()->ForceNetUpdate();
}
void UMCPerkComponent::OnRep_SkillLoadout() {OnPerksChanged.Broadcast();}
