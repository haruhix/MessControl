#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPerkTypes.h"
#include "MCPerkPickup.generated.h"

class AMCRewardChest;
class AMCToothCharacter;
class USphereComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Server overlap grants a stable row ID once; clients only display replicated loot. */
UCLASS()
class MESSCONTROL_API AMCPerkPickup : public AActor
{
    GENERATED_BODY()
public:
    AMCPerkPickup();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void InitializePickup(AMCRewardChest* Chest,int32 Index,FName ID,FText Name,FText Detail,EMCPerkPolarity Kind);
    void StartArming();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Rewards") bool TryCollect(AMCToothCharacter* Player);
    void MarkClaimed();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USphereComponent> Trigger;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UTextRenderComponent> DetailLabel;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly,Category="Rewards") FName PerkID;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly,Category="Rewards") FText DisplayName;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly,Category="Rewards") FText Description;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly,Category="Rewards") EMCPerkPolarity Polarity=EMCPerkPolarity::Positive;
    UPROPERTY(ReplicatedUsing=RefreshAppearance,BlueprintReadOnly,Category="Rewards") bool bClaimed=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") int32 LootIndex=INDEX_NONE;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") TObjectPtr<AMCRewardChest> RewardChest;
private:
    UFUNCTION() void RefreshAppearance();
    UFUNCTION() void Entered(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,
        int32 BodyIndex,bool bSweep,const FHitResult& Hit);
    UFUNCTION() void Left(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex);
    void Arm();
    FTimerHandle ArmTimer;
    TSet<TWeakObjectPtr<AActor>> MustReenter;
    bool bArmed=false;
};
