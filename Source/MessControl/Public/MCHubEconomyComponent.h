#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCHubEconomyTypes.h"
#include "MCCosmeticCatalog.h"
#include "MCHubEconomyComponent.generated.h"

class AMCPlayerState;

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCHubEconomySaveData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Version=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Coins=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> OwnedCosmetics;
    /** Catalog IDs, one entry per purchased stack/charge, waiting for a real run. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> PendingBoosters;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FMCHubPendingPurchase> PendingPurchases;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid ShopRotationId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ShopRotationSeed=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bShopStockGenerated=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> ShopStockIDs;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FMCCharacterCosmeticLoadout EquippedLoadout;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid LastAppliedRunId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RunCalciumCharges=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FMCGrillReward PendingGrillReward;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCHubEconomyChanged);

/** Personal authority-owned wallet and next-run inventory on PlayerState. No purchase RPC trusts client prices. */
UCLASS(ClassGroup=(MessControl), meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCHubEconomyComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCHubEconomyComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") int32 Coins=0;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") TArray<FName> OwnedCosmetics;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") TArray<FName> PendingBoosters;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") TArray<FMCHubPendingPurchase> PendingPurchases;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") FGuid ShopRotationId;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") int32 ShopRotationSeed=0;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") bool bShopStockGenerated=false;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") TArray<FName> ShopStockIDs;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") FText MigrationNotice;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") FMCCharacterCosmeticLoadout EquippedLoadout;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") int32 RunCalciumCharges=0;
    UPROPERTY(ReplicatedUsing=OnRep_Economy, BlueprintReadOnly, Category="Economy") FMCGrillReward LastGrillReward;
    UPROPERTY(BlueprintAssignable, Category="Economy") FMCHubEconomyChanged OnEconomyChanged;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Economy|Data") TSoftObjectPtr<UDataTable> CatalogTable;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Economy|Data") TSoftObjectPtr<UDataTable> GrillRewardsTable;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Economy|Data") TSoftObjectPtr<UDataTable> RulesTable;
    UFUNCTION(BlueprintPure, Category="Economy") TArray<FMCHubEconomyCatalogEntry> GetCatalog() const;
    UFUNCTION(BlueprintPure, Category="Economy") TArray<FMCHubEconomyCatalogEntry> GetShopCatalog() const;
    UFUNCTION(BlueprintPure, Category="Economy") TArray<FMCActiveSkillSlot> GetPendingActiveSlots() const;
    UFUNCTION(BlueprintPure, Category="Economy") bool IsInShopStock(FName ItemID) const;
    bool EnsureShopStock(const FGuid& RotationId,int32 Seed);
    bool FindCatalogEntry(FName ItemID,FMCHubEconomyCatalogEntry& Entry) const;
    UFUNCTION(BlueprintPure, Category="Economy") bool OwnsCosmetic(FName CosmeticID) const;
    UFUNCTION(BlueprintPure, Category="Economy") int32 QueuedStacks(FName ItemID) const;
    UFUNCTION(BlueprintPure, Category="Economy") bool IsCalciumEnabled() const;
    const FMCHubCatalogRow* FindCatalogItem(FName ItemID) const;
    FMCHubEconomyRulesRow GetRules() const;
    TArray<FName> GetGrillSymbols() const;
    bool CanPurchase(FName ItemID, FText& Error) const;
    /** Authority only; the controller must also validate the actual nearby shop. */
    bool TryPurchase(FName ItemID, FText& Error);
    bool CanPurchaseForSlot(FName ItemID,int32 DestinationSlot,FName ExpectedReplacedID,const FGuid& ExpectedInstanceID,FText& Error) const;
    bool TryPurchaseForSlot(FName ItemID,int32 DestinationSlot,FName ExpectedReplacedID,const FGuid& ExpectedInstanceID,FText& Error);
    /** Server objective payout, usable during the arena. No client RPC or purchase-context requirement.
     * Returns the actual increase after the wallet cap; zero never creates a cosmetic reward. */
    int32 CreditObjectiveCoins(int32 Amount);
    bool TryEquipLoadout(const FMCCharacterCosmeticLoadout& Loadout, FText& Error);
    /** Fish is reserved once; the server chooses and stores the result before accepting the attempt. */
    bool BeginGrillRoll(FGuid& Receipt, FMCGrillReward& Preview, FText& Error);
    bool CompleteGrillRoll(const FGuid& Receipt);
    bool CancelGrillRoll(const FGuid& Receipt);
    /** Snapshot settles an accepted receipt before travel/save can replace its owner. */
    FMCHubEconomySaveData ExportSave();
    bool ImportSave(const FMCHubEconomySaveData& Data, FText& Error);
    void CopyTo(UMCHubEconomyComponent* Target);
    /** Call after the new run resets perks. Never call on respawn with another ID. */
    bool ApplyPendingBoostersForRun(const FGuid& RunId, FText& Error);
    bool TryConsumeCalciumCharge();
    void RefundCalciumCharge();
    bool HasPendingGrillRoll() const { return PendingRoll.RollID.IsValid(); }
protected:
    virtual void BeginPlay() override;
private:
    bool IsAuthority() const;
    bool IsGatheringHub() const;
    bool IsRealRun() const;
    bool LoadTables() const;
    bool IsCatalogUsable(const FMCHubCatalogRow& Item, bool bIncludeSuspended=false) const;
    bool CanQueueItem(FName ItemID, const FMCHubCatalogRow& Item, int32 Stacks=1) const;
    bool ResolvePerk(FName PerkID,FMCPerkDefinition& Definition) const;
    TArray<FMCHubPendingPurchase> BuildPendingPlan(TArray<TArray<int32>>* LegacyIndices=nullptr) const;
    void RefreshMigrationNotice();
    bool SelectGrillReward(FMCGrillReward& Reward, FText& Error) const;
    void NotifyChanged();
    void AddCoinsClamped(int32 Amount);
    UFUNCTION() void OnRep_Economy();
    UPROPERTY(Transient) mutable TObjectPtr<UDataTable> LoadedCatalog;
    UPROPERTY(Transient) mutable TObjectPtr<UDataTable> LoadedRewards;
    UPROPERTY(Transient) mutable TObjectPtr<UDataTable> LoadedRules;
    UPROPERTY(Transient) FMCGrillReward PendingRoll;
    UPROPERTY(Transient) FGuid LastAppliedRunId;
    bool bMutating=false;
};
