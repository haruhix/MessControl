#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UMCPerkComponent::UMCPerkComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    PerkTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")));
}

void UMCPerkComponent::BeginPlay()
{
    Super::BeginPlay();
    LoadDefinitions();
    RebuildEffects();
}

void UMCPerkComponent::LoadDefinitions()
{
    if (bDefinitionsLoaded) return;
    bDefinitionsLoaded = true;
    LoadedTable = PerkTable.LoadSynchronous();
    if (LoadedTable && LoadedTable->GetRowStruct() != FMCPerkDefinition::StaticStruct())
    {
        UE_LOG(LogTemp, Error, TEXT("Perk table %s has an incompatible row struct"), *GetNameSafe(LoadedTable));
        LoadedTable = nullptr;
    }
    if (!LoadedTable)
        UE_LOG(LogTemp, Warning, TEXT("No usable perk table for %s: %s"), *GetNameSafe(GetOwner()), *PerkTable.ToString());
}

bool UMCPerkComponent::HasAuthority() const
{
    return GetOwner() && GetOwner()->HasAuthority();
}

bool UMCPerkComponent::IsDefinitionUsable(const FMCPerkDefinition& Definition)
{
    return Definition.MaxStacks > 0
        && (Definition.Polarity == EMCPerkPolarity::Positive || Definition.Polarity == EMCPerkPolarity::Negative)
        && (!Definition.EffectClass || !Definition.EffectClass->HasAnyClassFlags(CLASS_Abstract));
}

const FMCPerkDefinition* UMCPerkComponent::FindDefinition(FName PerkID) const
{
    return LoadedTable && !PerkID.IsNone()
        ? LoadedTable->FindRow<FMCPerkDefinition>(PerkID, TEXT("Perk lookup"), false) : nullptr;
}

bool UMCPerkComponent::GetPerkDefinition(FName PerkID, FMCPerkDefinition& Definition) const
{
    if (const FMCPerkDefinition* Row = FindDefinition(PerkID))
    {
        Definition = *Row;
        return true;
    }
    Definition = FMCPerkDefinition();
    return false;
}

int32 UMCPerkComponent::GetStacks(FName PerkID) const
{
    const FMCActivePerk* Active = ActivePerks.FindByPredicate([PerkID](const FMCActivePerk& Perk) { return Perk.PerkID == PerkID; });
    return Active ? Active->Stacks : 0;
}

bool UMCPerkComponent::CanGrantPerk(FName PerkID) const
{
    const FMCPerkDefinition* Definition = FindDefinition(PerkID);
    return Definition && IsDefinitionUsable(*Definition) && GetStacks(PerkID) < FMath::Clamp(Definition->MaxStacks, 1, 100);
}

bool UMCPerkComponent::ServerGrantPerk(FName PerkID)
{
    return ServerGrantPerks({ PerkID });
}

bool UMCPerkComponent::ServerGrantPerks(const TArray<FName>& PerkIDs)
{
    if (!HasAuthority() || bRebuilding || PerkIDs.IsEmpty() || PerkIDs.Num() > 100) return false;
    LoadDefinitions();
    TMap<FName, int32> Requested;
    for (FName ID : PerkIDs)
    {
        const FMCPerkDefinition* Definition = FindDefinition(ID);
        if (!Definition || !IsDefinitionUsable(*Definition)) return false;
        int32& Count = Requested.FindOrAdd(ID);
        ++Count;
        if (GetStacks(ID) + Count > FMath::Clamp(Definition->MaxStacks, 1, 100)) return false;
    }
    for (const TPair<FName, int32>& Request : Requested)
    {
        FMCActivePerk* Active = ActivePerks.FindByPredicate([&Request](const FMCActivePerk& Perk) { return Perk.PerkID == Request.Key; });
        if (Active) Active->Stacks += Request.Value;
        else
        {
            FMCActivePerk NewPerk;
            NewPerk.PerkID = Request.Key;
            NewPerk.Stacks = Request.Value;
            ActivePerks.Add(NewPerk);
        }
    }
    ActivePerks.Sort([](const FMCActivePerk& A, const FMCActivePerk& B) { return A.PerkID.LexicalLess(B.PerkID); });
    RebuildEffects();
    GetOwner()->ForceNetUpdate();
    return true;
}

bool UMCPerkComponent::RemovePerk(FName PerkID)
{
    if (!HasAuthority() || bRebuilding) return false;
    if (!ActivePerks.RemoveAll([PerkID](const FMCActivePerk& Perk) { return Perk.PerkID == PerkID; })) return false;
    RebuildEffects();
    GetOwner()->ForceNetUpdate();
    return true;
}

void UMCPerkComponent::ResetPerks()
{
    if (!HasAuthority() || bRebuilding) return;
    ActivePerks.Reset();
    RebuildEffects();
    GetOwner()->ForceNetUpdate();
}

void UMCPerkComponent::CopyPerksTo(UMCPerkComponent* Target) const
{
    if (!HasAuthority() || !Target || !Target->HasAuthority() || Target == this || Target->bRebuilding) return;
    Target->PerkTable = PerkTable;
    Target->LoadedTable = LoadedTable;
    Target->bDefinitionsLoaded = bDefinitionsLoaded;
    Target->ActivePerks = ActivePerks;
    Target->RebuildEffects();
    Target->GetOwner()->ForceNetUpdate();
}

void UMCPerkComponent::RebuildEffects()
{
    if (bRebuilding) return;
    TGuardValue<bool> Guard(bRebuilding, true);
    LoadDefinitions();
    if (!HasAuthority())
    {
        OnPerksChanged.Broadcast();
        return;
    }
    for (auto It = Effects.CreateIterator(); It; ++It)
    {
        const FMCPerkDefinition* Definition = FindDefinition(It.Key());
        if (GetStacks(It.Key()) <= 0 || !Definition || !IsDefinitionUsable(*Definition))
        {
            if (HasAuthority() && It.Value()) It.Value()->OnRemoved(It.Key());
            AppliedStacks.Remove(It.Key());
            It.RemoveCurrent();
        }
    }

    // Keep lifecycle callbacks in stable ID order.
    TArray<FMCActivePerk> OrderedPerks = ActivePerks;
    OrderedPerks.Sort([](const FMCActivePerk& A, const FMCActivePerk& B) { return A.PerkID.LexicalLess(B.PerkID); });
    for (const FMCActivePerk& Active : OrderedPerks)
    {
        const FMCPerkDefinition* Definition = FindDefinition(Active.PerkID);
        if (!Definition || !IsDefinitionUsable(*Definition) || Active.Stacks <= 0) continue;
        TObjectPtr<UMCPerkEffect>& Effect = Effects.FindOrAdd(Active.PerkID);
        UClass* Class = Definition->EffectClass ? Definition->EffectClass.Get() : UMCPerkEffect::StaticClass();
        if (!Effect || Effect->GetClass() != Class)
        {
            if (HasAuthority() && Effect) Effect->OnRemoved(Active.PerkID);
            Effect = NewObject<UMCPerkEffect>(this, Class);
            Effect->OwnerComponent = this;
            AppliedStacks.Remove(Active.PerkID);
        }
        const int32 Stacks = FMath::Clamp(Active.Stacks, 1, FMath::Clamp(Definition->MaxStacks, 1, 100));
        if (HasAuthority())
        {
            if (!AppliedStacks.Contains(Active.PerkID)) Effect->OnApplied(Active.PerkID, *Definition, Stacks);
            else if (AppliedStacks[Active.PerkID] != Stacks) Effect->OnStacksChanged(Active.PerkID, *Definition, Stacks);
        }
        AppliedStacks.Add(Active.PerkID, Stacks);
    }
    OnPerksChanged.Broadcast();
}

void UMCPerkComponent::OnRep_ActivePerks()
{
    RebuildEffects();
}

void UMCPerkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bRebuilding = true;
    if (HasAuthority())
        for (const TPair<FName, TObjectPtr<UMCPerkEffect>>& Effect : Effects)
            if (Effect.Value) Effect.Value->OnRemoved(Effect.Key);
    Effects.Reset();
    AppliedStacks.Reset();
    Super::EndPlay(EndPlayReason);
}

void UMCPerkComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMCPerkComponent, ActivePerks);
}
