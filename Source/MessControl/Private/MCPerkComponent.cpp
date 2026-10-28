#include "MCPerkComponent.h"
#include "MCCombatPassiveSubsystem.h"
#include "MCDefenseSprayPassives.h"
#include "MCPerkEffect.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UMCPerkComponent::UMCPerkComponent()
{
    ActiveSkillSlots.SetNum(ActiveSkillSlotCount);
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.1f;
    SetIsReplicatedByDefault(true);
    PerkTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")));
}

void UMCPerkComponent::BeginPlay()
{
    Super::BeginPlay();
    LoadDefinitions();
    MigrateLegacyOwnedState();
    RefreshLegacyProjection();
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
    // The perforator is retained for authored assets, but disabled in every reward pool.
    return Definition.bEnabled && Definition.MaxStacks > 0 && uint8(Definition.Kind)<=uint8(EMCPerkKind::ToolUpgrade)
        && FMath::IsFinite(Definition.CooldownSeconds) && Definition.CooldownSeconds>=0
        && Definition.ToolUpgrade!=EMCToolUpgrade::Buffer
        && uint8(Definition.ToolUpgrade)<=uint8(EMCToolUpgrade::Watergun)
        && uint8(Definition.Polarity)<=uint8(EMCPerkPolarity::Risky)
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

bool UMCPerkComponent::HasToolUpgrade(EMCToolUpgrade Kind) const
{
    if(Kind==EMCToolUpgrade::None || Kind==EMCToolUpgrade::Buffer || uint8(Kind)>4) return false;
    if(ValidationToolPerks) for(FName ID:*ValidationToolPerks)
        if(const auto* Row=FindDefinition(ID);Row && IsDefinitionUsable(*Row) && Row->ToolUpgrade==Kind) return true;
    if(const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr)
        if((GS->TeamToolUpgrades&(1<<(uint8(Kind)-1))) && LoadedTable)
            for(FName ID:LoadedTable->GetRowNames()) {
                const auto* Row=FindDefinition(ID);
                if(Row && IsDefinitionUsable(*Row) && Row->ToolUpgrade==Kind) return true;
            }
    for(const auto& Active:ActivePerks) if(Active.Stacks>0)
        if(const auto* Row=FindDefinition(Active.PerkID);Row && IsDefinitionUsable(*Row) && Row->ToolUpgrade==Kind) return true;
    return false;
}
TArray<FName> UMCPerkComponent::GetToolRewardIDs() const
{
    TArray<FName> IDs;
    if(LoadedTable) for(FName ID:LoadedTable->GetRowNames()) {
        const auto* Row=FindDefinition(ID);
        if(Row && IsDefinitionUsable(*Row) && Row->ToolUpgrade!=EMCToolUpgrade::None
            && (Row->Rarity==EMCPerkRarity::Rare || Row->Rarity==EMCPerkRarity::Legendary)) IDs.Add(ID);
    }
    IDs.Sort([](FName A,FName B){return A.LexicalLess(B);});return IDs;
}
bool UMCPerkComponent::ServerGrantReward(FName PerkID)
{
    if(!HasAuthority() || !CanGrantPerk(PerkID)) return false;
    const auto* Row=FindDefinition(PerkID);
    if(Row->ToolUpgrade==EMCToolUpgrade::None || Row->Rarity!=EMCPerkRarity::Legendary) return ServerGrantPerk(PerkID);
    auto* GS=GetWorld()->GetGameState<AMCGameState>();if(!GS) return false;
    if(!ServerGrantPerk(PerkID)) return false;
    for(const auto& State:GS->PlayerArray) if(auto* Player=Cast<AMCPlayerState>(State.Get()))
        if(!Player->IsSpectator() && !Player->IsOnlyASpectator() && Player->Perks && Player->Perks!=this
            && Player->Perks->GetPerkTable()==LoadedTable && Player->Perks->CanGrantPerk(PerkID)) Player->Perks->ServerGrantPerk(PerkID);
    GS->TeamToolUpgrades|=1<<(uint8(Row->ToolUpgrade)-1);GS->ForceNetUpdate();return true;
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
            if (HasAuthority() && It.Value()) {It.Value()->CancelActivation();It.Value()->OnRemoved(It.Key());}
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
        const int32 Stacks = FMath::Clamp(Active.Stacks,1,FMath::Clamp(Definition->MaxStacks,1,Definition->bConsumableBooster?10000:100));
        if (HasAuthority())
        {
            if (!AppliedStacks.Contains(Active.PerkID)) Effect->OnApplied(Active.PerkID, *Definition, Stacks);
            else if (AppliedStacks[Active.PerkID] != Stacks) Effect->OnStacksChanged(Active.PerkID, *Definition, Stacks);
        }
        AppliedStacks.Add(Active.PerkID, Stacks);
    }
    if(GetStacks(TEXT("B2_ReturnTrip"))<=0) DeliverySpeedUntil=-100;
    if(GetStacks(TEXT("B2_CleanAcceleration"))<=0 && GetStacks(TEXT("P17"))<=0) CleanSpeedUntil=-100;
    UMCCombatPassiveSubsystem::PruneOwner(this);
    UMCDefenseSprayPassives::PruneOwner(this);
    OnPerksChanged.Broadcast();
}

void UMCPerkComponent::OnRep_ActivePerks()
{
    RebuildEffects();
}

void UMCPerkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelSkillPreparations();
    ClearRogueRuntime();
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
    DOREPLIFETIME(UMCPerkComponent,ActiveSkillSlots);
    DOREPLIFETIME(UMCPerkComponent,PassivePerkIDs);
    DOREPLIFETIME(UMCPerkComponent,OwnedToolPerkIDs);
    DOREPLIFETIME_CONDITION(UMCPerkComponent,LegacyUnappliedBoosterIDs,COND_OwnerOnly);
    DOREPLIFETIME(UMCPerkComponent,CounterReadyUntil);
    DOREPLIFETIME(UMCPerkComponent,CleanSpeedUntil);
    DOREPLIFETIME(UMCPerkComponent,DeliverySlowUntil);
    DOREPLIFETIME(UMCPerkComponent,DeliverySpeedUntil);
    DOREPLIFETIME(UMCPerkComponent,BoosterQueue);DOREPLIFETIME(UMCPerkComponent,bGrenadeArmed);DOREPLIFETIME(UMCPerkComponent,FuryUntil);
    DOREPLIFETIME(UMCPerkComponent,FuryDirection);
}
