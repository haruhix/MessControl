#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPerkTypes.h"
#include "MCRewardChest.generated.h"

class UDataTable;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class USphereComponent;
class AMCRewardDropZone;
class AMCPerkPickup;
class AMCToothCharacter;
class UMCPerkComponent;

UENUM(BlueprintType)
enum class EMCRewardSelectionPolicy : uint8 { ChooseOne, CollectAll };

UENUM(BlueprintType)
enum class EMCRewardChestStage : uint8 { Telegraph, Falling, Landed, Lockpicking, Opening, Open, Exhausted };

/** Server-owned rewards, deterministic presentation, no rigid-body simulation. */
UCLASS()
class MESSCONTROL_API AMCRewardChest : public AActor
{
    GENERATED_BODY()
public:
    AMCRewardChest();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void InitializeReward(FVector Landing,FVector Start,int32 Seed,UDataTable* Table,
        EMCRewardSelectionPolicy Policy,AMCRewardDropZone* Zone);
    FVector GetPlacementHalfExtent() const;
    /** X points out of the keyhole; author a Lockpick socket on replacement meshes. */
    FTransform GetLockpickContact() const;
    bool CanReachLockpick(const AMCToothCharacter* Player) const;
    static bool HasValidLoot(UDataTable* Table,const UMCPerkComponent* Recipient=nullptr);
    bool BeginLockpicking(AMCToothCharacter* Player);
    bool TryChooseCard(AMCToothCharacter* Player,int32 Index);
    AMCToothCharacter* GetOpener() const { return OpeningPlayer; }
    /** Compatibility for saved references; world pickup collection is no longer used. */
    bool TryClaim(AMCPerkPickup* Pickup,AMCToothCharacter* Player);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Rewards") void ResetPlacedReward();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> Scene;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBoxComponent> Solid;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> LidPivot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Lid;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Telegraph;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USphereComponent> Approach;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.05",ClampMax="2")) float ModelScale=.35f;
    /** Lid seating adjustment in mesh units, applied before ModelScale. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards") float LidSeatOffset=0.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1")) float TelegraphSeconds=1.2f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1")) float FallSeconds=1.2f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1")) float OpeningSeconds=.7f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1",ClampMax="60")) float LockpickingSeconds=5.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="100")) float OpenRadius=320.f;
    UPROPERTY(meta=(DeprecatedProperty,DeprecationMessage="Rewards are selected using HUD cards.")) TSubclassOf<AMCPerkPickup> PickupClass;
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="Rewards") bool bPlacedReward=false;
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="Rewards") TObjectPtr<AMCRewardDropZone> PlacedDropZone;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards") TSoftObjectPtr<UDataTable> PerkTable;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadOnly,Category="Rewards") EMCRewardSelectionPolicy SelectionPolicy=EMCRewardSelectionPolicy::ChooseOne;
    UPROPERTY(ReplicatedUsing=RefreshPresentation,BlueprintReadOnly,Category="Rewards") EMCRewardChestStage Stage=EMCRewardChestStage::Telegraph;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") FVector LandingPoint=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") FVector FallStart=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") double StageStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") TArray<FName> LootIDs;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") EMCPerkPolarity Polarity=EMCPerkPolarity::Positive;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") uint8 ClaimedMask=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards") TObjectPtr<AMCToothCharacter> OpeningPlayer;
private:
    UFUNCTION() void RefreshPresentation();
    void ConfigureGeometry();
    void PollApproach();
    void ReleaseOpener();
    void CancelOpening();
    bool OpenerCanContinue() const;
    bool IsLivingPlayer(const AMCToothCharacter* Player) const;
    bool HasClearLanding() const;
    void SetStage(EMCRewardChestStage Next);
    void FinishReward(bool bRequeue);
    double ServerNow() const;
    UPROPERTY() TObjectPtr<UDataTable> RewardTable;
    UPROPERTY() TObjectPtr<AMCRewardDropZone> DropZone;
    UPROPERTY(ReplicatedUsing=RefreshPresentation) bool bRewardInitialized=false;
    FTimerHandle ApproachTimer;
    int32 RollSeed=0;
    bool bClaimInProgress=false;
    bool bReported=false;
};
