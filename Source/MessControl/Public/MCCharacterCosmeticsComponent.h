#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCCosmeticCatalog.h"
#include "MCCharacterCosmeticsComponent.generated.h"

class UStaticMeshComponent;

/** Accessory presentation only. Purchases, ownership and persistence belong to the player economy. */
UCLASS(ClassGroup=(Appearance),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCCharacterCosmeticsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCCharacterCosmeticsComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;

    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Cosmetics") TObjectPtr<UDataTable> Catalog;
    UPROPERTY(ReplicatedUsing=OnRep_Loadout,BlueprintReadOnly,Category="Cosmetics") FMCCharacterCosmeticLoadout EquippedLoadout;
    /** Authority-only; call after the economy has accepted the owned selection. No client RPC is exposed here. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Cosmetics") bool SetEquippedLoadout(const FMCCharacterCosmeticLoadout& Loadout);
    /** Local fitting preview; it never mutates or replicates EquippedLoadout. */
    UFUNCTION(BlueprintCallable,Category="Cosmetics") bool PreviewLoadout(const FMCCharacterCosmeticLoadout& Loadout);
    UFUNCTION(BlueprintCallable,Category="Cosmetics") void RestoreAuthoritativeLoadout();
    /** Reattach after an authored appearance/rig change. Missing attachment bones hide the item safely. */
    UFUNCTION(BlueprintCallable,Category="Cosmetics") void RefreshPresentation();
    UFUNCTION(BlueprintPure,Category="Cosmetics") UDataTable* GetCatalog();
    UFUNCTION(BlueprintPure,Category="Cosmetics") TArray<UStaticMeshComponent*> GetAccessoryParts() const;
protected:
    UFUNCTION() void OnRep_Loadout();
private:
    void ClearParts();
    void SyncVisibility();
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    FMCCharacterCosmeticLoadout Preview;
    bool bPreviewing=false;
};
