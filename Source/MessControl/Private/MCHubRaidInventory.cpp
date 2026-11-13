#include "MCHubEconomyComponent.h"

#include "MCHubGameState.h"
#include "MCPerkComponent.h"
#include "Engine/World.h"
#include "Templates/UnrealTemplate.h"

#define LOCTEXT_NAMESPACE "MCHubRaidInventory"

bool UMCHubEconomyComponent::IsRaidPerkKind(const FMCPerkDefinition& Definition,EMCPerkKind Kind)
{
    if(Definition.ToolUpgrade!=EMCToolUpgrade::None || !UMCPerkComponent::IsDefinitionUsable(Definition)) return false;
    // The authored ActiveSkills family includes tools such as Rocket and Air
    // with finite charges. Their old flag controls in-arena acquisition.
    if(Kind==EMCPerkKind::Consumable)
        return Definition.Category==TEXT("ActiveSkills") && Definition.InitialCharges>0 && Definition.InitialCharges<=10000;
    return !Definition.bConsumableBooster && (Kind==EMCPerkKind::Active || Kind==EMCPerkKind::Passive);
}

bool UMCHubEconomyComponent::GetRaidPerkDefinition(FName PerkID,FMCPerkDefinition& Definition) const
{
    if(!ResolvePerk(PerkID,Definition)) return false;
    Definition.Kind=UMCPerkComponent::ResolvePerkKind(Definition,PerkID);
    return IsRaidPerkKind(Definition,Definition.Kind);
}

int32 UMCHubEconomyComponent::GetRaidPerkCopies(FName PerkID) const
{
    const auto* Entry=RaidPerkStock.FindByPredicate([PerkID](const auto& Row){return Row.PerkID==PerkID;});
    return Entry?Entry->Copies:0;
}

bool UMCHubEconomyComponent::CanAddRaidPerkCopies(FName PerkID,int32 Copies) const
{
    if(PerkID.IsNone() || Copies<=0) return false;
    const int32 Owned=GetRaidPerkCopies(PerkID);
    return int64(Owned)+Copies<=MaximumRaidStockCopies && (Owned>0 || RaidPerkStock.Num()<MaximumRaidStockEntries);
}

void UMCHubEconomyComponent::AddRaidPerkCopies(FName PerkID,int32 Copies,int32 LegacyCharges)
{
    auto* Entry=RaidPerkStock.FindByPredicate([PerkID](const auto& Row){return Row.PerkID==PerkID;});
    if(!Entry) {
        Entry=&RaidPerkStock.AddDefaulted_GetRef();
        Entry->PerkID=PerkID;
    }
    Entry->Copies+=Copies;
    if(LegacyCharges>0) Entry->LegacyChargePacks.Add(LegacyCharges);
}

void UMCHubEconomyComponent::MigrateRaidPerkStock()
{
    // An older purchase used a receipt per perk, while the oldest queue used
    // catalog IDs. Preserve unknown receipts and never count an ambiguous ID twice.
    TSet<FGuid> Seen,Duplicates;
    for(const auto& Purchase:PendingPurchases) {
        if(Seen.Contains(Purchase.InstanceID)) Duplicates.Add(Purchase.InstanceID);
        Seen.Add(Purchase.InstanceID);
    }
    PendingPurchases.RemoveAll([&](const auto& Purchase) {
        FMCPerkDefinition Definition;
        if(!Purchase.InstanceID.IsValid() || Duplicates.Contains(Purchase.InstanceID)
            || !GetRaidPerkDefinition(Purchase.PerkID,Definition) || !CanAddRaidPerkCopies(Purchase.PerkID,1)) return false;
        if(Definition.Kind==EMCPerkKind::Consumable ? Purchase.Charges<1 || Purchase.Charges>10000 : Purchase.Charges!=INDEX_NONE)
            return false;
        AddRaidPerkCopies(Purchase.PerkID,1,Purchase.Charges);
        return true;
    });
    // Old unstructured queues paid for individual charges. Keep the existing
    // grouped-per-perk pack semantics when moving that inventory to the picker.
    TMap<FName,int32> LegacyPackCharges;
    for(FName CatalogID:PendingBoosters) {
        const auto* Item=FindCatalogItem(CatalogID);
        FMCPerkDefinition Definition;
        if(Item && GetRaidPerkDefinition(Item->PerkID,Definition) && Definition.Kind==EMCPerkKind::Consumable)
            ++LegacyPackCharges.FindOrAdd(Item->PerkID);
    }
    TSet<FName> MigratedPacks;
    for(const auto& [ID,Charges]:LegacyPackCharges) if(CanAddRaidPerkCopies(ID,1)) {
        AddRaidPerkCopies(ID,1,Charges);
        MigratedPacks.Add(ID);
    }
    PendingBoosters.RemoveAll([&](FName CatalogID) {
        const auto* Item=FindCatalogItem(CatalogID);
        FMCPerkDefinition Definition;
        if(!Item || !GetRaidPerkDefinition(Item->PerkID,Definition)) return false;
        if(Definition.Kind==EMCPerkKind::Consumable) return MigratedPacks.Contains(Item->PerkID);
        if(!CanAddRaidPerkCopies(Item->PerkID,1)) return false;
        AddRaidPerkCopies(Item->PerkID,1);
        return true;
    });
}

bool UMCHubEconomyComponent::ValidateRaidLoadout(const FMCHubRaidLoadout& Loadout,FText& Error) const
{
    Error=FText::GetEmpty();
    if(Loadout.ActivePerks.Num()>FMCHubRaidLoadout::SlotsPerKind || Loadout.PassivePerks.Num()>FMCHubRaidLoadout::SlotsPerKind) {
        Error=LOCTEXT("SlotLimit","Можно взять до четырёх активных и четырёх пассивных перков.");
        return false;
    }
    TSet<FName> IDs;
    const auto ValidateKind=[&](const TArray<FName>& Perks,EMCPerkKind Kind) {
        for(FName ID:Perks) {
            FMCPerkDefinition Definition;
            if(IDs.Contains(ID) || GetRaidPerkCopies(ID)<1 || !GetRaidPerkDefinition(ID,Definition)
                || (Definition.Kind!=Kind && !(Kind==EMCPerkKind::Active && Definition.Kind==EMCPerkKind::Consumable))) return false;
            IDs.Add(ID);
        }
        return true;
    };
    if(ValidateKind(Loadout.ActivePerks,EMCPerkKind::Active) && ValidateKind(Loadout.PassivePerks,EMCPerkKind::Passive)) return true;
    Error=LOCTEXT("InvalidSelection","Один из выбранных перков недоступен. Проверь свой запас.");
    return false;
}

bool UMCHubEconomyComponent::TrySetRaidLoadout(const FMCHubRaidLoadout& Loadout,FText& Error)
{
    const auto* Hub=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    if(!IsAuthority() || bMutating || !Hub || Hub->Phase!=EMCHubPhase::Selecting) return false;
    if(!ValidateRaidLoadout(Loadout,Error)) return false;
    if(RaidLoadout==Loadout) return true;
    TGuardValue<bool> Guard(bMutating,true);
    RaidTravelTicket.Invalidate();
    RaidLoadout=Loadout;
    NotifyChanged();
    return true;
}

bool UMCHubEconomyComponent::CanStartRaid(FText& Error) const
{
    return IsAuthority() && !bMutating && ValidateRaidLoadout(RaidLoadout,Error);
}

bool UMCHubEconomyComponent::LockRaidLoadout(const FGuid& Ticket,FText& Error)
{
    const auto* Hub=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    if(!Hub || Hub->Phase!=EMCHubPhase::Selecting || !Ticket.IsValid() || !CanStartRaid(Error)) return false;
    RaidTravelTicket=Ticket;
    return true;
}

void UMCHubEconomyComponent::UnlockRaidLoadout(const FGuid& Ticket)
{
    if(IsAuthority() && !bMutating && RaidTravelTicket==Ticket) RaidTravelTicket.Invalidate();
}

#undef LOCTEXT_NAMESPACE
