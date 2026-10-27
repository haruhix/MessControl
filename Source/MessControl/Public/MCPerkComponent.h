#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCPerkTypes.h"
#include "MCPerkComponent.generated.h"

class UMCPerkEffect;
class AMCToothCharacter;
class AMCGameState;
class UPointLightComponent;
class UTextRenderComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCOnPerksChanged);

/** Run perks live on PlayerState, so death and pawn replacement do not erase rewards. */
UCLASS(ClassGroup=(MessControl), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCPerkComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCPerkComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    static UMCPerkComponent* ForActor(const AActor* Actor);
    float RogueValue(EMCRogueStat Stat,float Base=1.f,AActor* Target=nullptr,FName ExtraStack=NAME_None) const;
    float WeaponDamage(float Base,AActor* Target) const;
    float IncomingDamage(float Base,EMCRogueDamageKind Kind) const;
    void NotifyWeaponHit(AActor* Target);
    void NotifyDodgedAttack();
    void NotifyCleaned(FVector Point);
    void NotifyFoodDelivered();
    int32 FoodExperienceBonus(int32 Base);
    void TryWaterRicochet(AActor* First,float BaseDamage,FVector HitPoint);
    void RefreshRogueTuning();
    bool IsRogueEligible(FName ID) const;
    bool HasRunEvent(FName Event) const;
    bool IsNightActive() const;
    bool HasNearbyAlly(float Radius) const;
    int32 RoguePolarityStacks(EMCPerkPolarity Polarity) const;
    bool BuildRogueOffer(EMCPerkPolarity Polarity,int32 Seed,TArray<FName>& Out,bool Early=false,bool RareOnly=false,
        bool GuaranteeRare=false,const TArray<FName>& Excluded=TArray<FName>(),bool IncludeTools=true) const;
    FString RoguePreview(FName ID) const;
    bool GetFogNavigation(FVector& Point) const;
    void AddBoosterCharge(FName ID);
    bool ActivateBooster();
    /** Stage chest only: apply one supported booster immediately to the current team. No charges or perk stacks. */
    bool ActivateTeamStageBooster(FName ID);
    /** Stage reward personal choice: activate on this recipient; no team fan-out or stored charge. */
    bool ActivatePersonalStageBooster(FName ID);
    /** Timeout awards are sealed immediately; an incapacitated recipient activates on recovery in this run. */
    bool AcceptTimedStageBooster(FName ID);
    static bool IsTeamStageBoosterID(FName ID);
    void OnRogueKnockdown();
    bool IsFuryActive() const;
    void TickRogueBoosters(float Dt);
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Boosters") TArray<FName> BoosterQueue;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Boosters") bool bGrenadeArmed=false;
    UPROPERTY(Replicated) double FuryUntil=-100;
    bool bRogueExtraHit=false;
    UPROPERTY(Replicated) double CounterReadyUntil=-100;
    UPROPERTY(Replicated) double CleanSpeedUntil=-100;
    UPROPERTY(Replicated) double DeliverySlowUntil=-100;

    /** Authority-only API, intentionally not a client RPC. Reward actors validate claims first. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    bool ServerGrantPerk(FName PerkID);
    /** Chest/shop grant: legendary tools additionally unlock for the entire run's team. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Perks") bool ServerGrantReward(FName PerkID);
    UFUNCTION(BlueprintPure,Category="Perks") bool HasToolUpgrade(EMCToolUpgrade Kind) const;

    /** Validate every requested stack before changing state; a batch cannot partially apply. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    bool ServerGrantPerks(const TArray<FName>& PerkIDs);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    bool RemovePerk(FName PerkID);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Perks")
    void ResetPerks();

    UFUNCTION(BlueprintPure, Category="Perks")
    bool CanGrantPerk(FName PerkID) const;

    UFUNCTION(BlueprintPure, Category="Perks")
    int32 GetStacks(FName PerkID) const;

    UFUNCTION(BlueprintPure, Category="Perks")
    bool GetPerkDefinition(FName PerkID, FMCPerkDefinition& Definition) const;

    const FMCPerkDefinition* FindDefinition(FName PerkID) const;
    /** Stable list shared by the host's F3 menu and its authoritative grant command. */
    TArray<FName> GetToolRewardIDs() const;

    UFUNCTION(BlueprintPure, Category="Perks")
    UDataTable* GetPerkTable() const { return LoadedTable; }

    void CopyPerksTo(UMCPerkComponent* Target) const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Perks")
    TSoftObjectPtr<UDataTable> PerkTable;

    UPROPERTY(ReplicatedUsing=OnRep_ActivePerks, BlueprintReadOnly, Category="Perks")
    TArray<FMCActivePerk> ActivePerks;

    UPROPERTY(BlueprintAssignable, Category="Perks")
    FMCOnPerksChanged OnPerksChanged;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void OnRep_ActivePerks();

    void LoadDefinitions();
    void RebuildEffects();
    bool HasAuthority() const;
    static bool IsDefinitionUsable(const FMCPerkDefinition& Definition);

    UPROPERTY(Transient)
    TObjectPtr<UDataTable> LoadedTable;

    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UMCPerkEffect>> Effects;

    TMap<FName, int32> AppliedStacks;
    bool bDefinitionsLoaded = false;
    bool bRebuilding = false;
    bool ActivationMatches(EMCRogueActivation Condition,AActor* Target) const;
    bool HasRecentAllyHit(AActor* Target) const;
    bool HasMeaningfulRogueChange(FName ID) const;
    void ClearRogueRuntime();
    TWeakObjectPtr<AMCToothCharacter> TunedHero;
    TWeakObjectPtr<UPointLightComponent> NightLight;
    TWeakObjectPtr<UTextRenderComponent> GrenadeCue;
    TArray<FName> PendingStageBoosters;
    TWeakObjectPtr<AMCGameState> PendingStageGame;
    FGuid PendingStageLoadId;
    TMap<TWeakObjectPtr<AActor>,double> RecentWeaponHits;
    TArray<TWeakObjectPtr<AActor>> RogueActors;
    double NextCounterAt=-100,NextTeamStrikeAt=-100,NextCleanHealAt=-100,NextFoamAt=-100;
    double NextRicochetAt=-100;
    double DeliveryXPResidual=0;
    UPROPERTY(Replicated) FVector FuryDirection=FVector::ForwardVector;
    uint16 FuryMotionID=0;
    bool bFuryMotion=false;
    double NextFuryHitAt=-100;
    void RogueBurst(bool Grenade,const TArray<AMCToothCharacter*>& TeamOrigins=TArray<AMCToothCharacter*>());
};
