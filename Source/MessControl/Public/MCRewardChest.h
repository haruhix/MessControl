#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPerkTypes.h"
#include "MCRewardChest.generated.h"

class UDataTable;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USphereComponent;
class UTextRenderComponent;
class AMCRewardDropZone;
class AMCPerkPickup;
class AMCToothCharacter;
class AMCPlayerState;
class UMCPerkComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EMCRewardSelectionPolicy : uint8 { ChooseOne, CollectAll };

UENUM(BlueprintType)
enum class EMCRewardChestStage : uint8 { Telegraph, Falling, Landed, Lockpicking, Opening, Open, Exhausted, MimicSwallowing, MimicOccupied };

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
    /** One stage chest, with an independent immediate booster choice for each participant. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Rewards")
    void InitializeTeamStageReward(FVector Landing,FVector Start,int32 Seed,UDataTable* Table,AMCRewardDropZone* Zone);
    UPROPERTY(ReplicatedUsing=RefreshPresentation,BlueprintReadOnly,Category="Rewards") bool bTeamStageBooster=false;
    FVector GetPlacementHalfExtent() const;
    /** X points out of the keyhole; author a Lockpick socket on replacement meshes. */
    FTransform GetLockpickContact() const;
    bool CanReachLockpick(const AMCToothCharacter* Player) const;
    static bool HasValidLoot(UDataTable* Table,const UMCPerkComponent* Recipient=nullptr);
    static EMCPerkRarity RollToolRarity(int32 Seed,float LegendaryPercent,float RarePercent);
    bool BeginLockpicking(AMCToothCharacter* Player);
    bool TryChooseCard(AMCToothCharacter* Player,int32 Index);
    /** Authority-side entitlement/interaction query; the ledger is keyed by PlayerState, not pawn. */
    bool HasStageChoiceFor(const AMCToothCharacter* Player) const;
    /** Re-send only this player's outstanding stage offer, e.g. after a rejected/stale choice RPC. */
    bool PublishStageChoiceFor(AMCToothCharacter* Player);
    bool ConfirmStageChoicePresented(AMCToothCharacter* Player);
    /** Authority clock for a captured recipient: retries and replacement never extend the Open-stage deadline. */
    double GetStageChoiceDeadline(const AMCPlayerState* PlayerState) const;
    bool ConfirmRogueContract(AMCToothCharacter* Player);
    bool DeclineRogueReward(AMCToothCharacter* Player);
    FMCRogueChestOffer GetRogueOffer() const;
    void PublishRogueOffer();
    bool PrepareRogueLoot(UMCPerkComponent* Perks);
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadOnly,Category="Rewards|Roguelike") EMCRogueRewardKind RogueKind=EMCRogueRewardKind::Ordinary;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards|Roguelike") TArray<FName> ContractRewards;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards|Roguelike") TArray<FName> ContractCurses;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards|Roguelike") FName ContractCurse;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards|Roguelike") FName ContractReward;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards|Roguelike") uint8 ContractStep=0;
    AMCToothCharacter* GetOpener() const { return OpeningPlayer; }
    UFUNCTION(BlueprintPure,Category="Rewards|Mimic") AMCToothCharacter* GetCapturedPlayer() const { return CaptivePlayer; }
    UFUNCTION(BlueprintPure,Category="Rewards|Mimic") bool IsMimicActive() const;
    bool IsHoldingCaptive() const;
    UFUNCTION(BlueprintPure,Category="Rewards|Mimic") FVector GetMimicCaptureLocation() const;
    UFUNCTION(BlueprintPure,Category="Rewards|Mimic") float GetMimicSwallowAlpha() const;
    UFUNCTION(BlueprintPure,Category="Rewards|Mimic") float RescueProgress() const;
    UFUNCTION(BlueprintPure,Category="Rewards|Mimic") bool CanRescue(const AMCToothCharacter* Player) const;
    bool BeginRescue(AMCToothCharacter* Player);
    void EndRescue(AMCToothCharacter* Player);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Rewards|Mimic") void ForceMimicForTest(bool Enabled=true);
    /** Compatibility for saved references; world pickup collection is no longer used. */
    bool TryClaim(AMCPerkPickup* Pickup,AMCToothCharacter* Player);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Rewards") void ResetPlacedReward();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> Scene;
    /** Grounded cosmetic deformation; gameplay collision remains on RewardRoot. */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Rewards|Presentation") TObjectPtr<USceneComponent> PresentationPivot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBoxComponent> Solid;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> LidPivot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Lid;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Telegraph;
    /** Explicit authored surface survives native/BP component-template reconstruction. Null preserves existing materials. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards|Presentation") TObjectPtr<UMaterialInterface> SurfaceMaterialOverride;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> MimicMouth;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> MimicLowerTeeth;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> MimicUpperTeeth;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USphereComponent> Approach;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UTextRenderComponent> RogueLabel;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Rewards|Presentation") TObjectPtr<UStaticMeshComponent> ClaimRing;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Rewards|Presentation") TObjectPtr<UInstancedStaticMeshComponent> ClaimSparks;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.05",ClampMax="2")) float ModelScale=.35f;
    /** Lid seating adjustment in mesh units, applied before ModelScale. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards") float LidSeatOffset=0.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1")) float TelegraphSeconds=1.2f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1")) float FallSeconds=1.2f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1")) float OpeningSeconds=.7f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="0.1",ClampMax="60")) float LockpickingSeconds=5.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Rewards",meta=(ClampMin="100")) float OpenRadius=320.f;
    /** Percent per non-mimic chest, separate from ordinary card weights. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards|Tools",meta=(ClampMin="0",ClampMax="100")) float LegendaryToolChancePercent=.02f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards|Tools",meta=(ClampMin="0",ClampMax="100")) float RareToolChancePercent=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Rewards|Mimic",meta=(ClampMin="0",ClampMax="1")) float MimicChance=.2f;
    UPROPERTY(EditDefaultsOnly,Replicated,BlueprintReadOnly,Category="Rewards|Mimic",meta=(ClampMin="0.1",ClampMax="5")) float MimicSwallowSeconds=.8f;
    UPROPERTY(EditDefaultsOnly,Replicated,BlueprintReadOnly,Category="Rewards|Mimic",meta=(ClampMin="0.1",ClampMax="30")) float RescueSeconds=3.f;
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
    UPROPERTY(ReplicatedUsing=RefreshPresentation,BlueprintReadOnly,Category="Rewards|Mimic") bool bMimic=false;
    UPROPERTY(ReplicatedUsing=RefreshPresentation,BlueprintReadOnly,Category="Rewards|Mimic") TObjectPtr<AMCToothCharacter> CaptivePlayer;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards|Mimic") TObjectPtr<AMCToothCharacter> RescuePlayer;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rewards|Mimic") double RescueStartedAt=0;
private:
    friend class FMCStageRewardBotSelectionTest;
    friend class FMCStageRewardIndividualClaimTest;
    friend class FMCStageRewardReplacementTest;
    friend class FMCStageRewardTerminalDeathTest;
    friend class FMCStageRewardArrivalOverlapTest;
    friend class FMCStageRewardTimeoutTest;
    UFUNCTION() void RefreshPresentation();
    void ConfigureGeometry();
    void CacheRewardEffectsVisibility();
    void UpdateRewardEffectsVisibility(bool Suppressed);
    void UpdateArrivalPresentation(float Age);
    void UpdateClaimPresentation(float Age);
    void UpdateSparkPresentation(float Progress,float Opacity,float Width,bool bTouchdown);
    void ResetPresentationDeformation();
    void PollApproach();
    void ReleaseOpener();
    void CancelOpening();
    bool OpenerCanContinue() const;
    bool IsLivingPlayer(const AMCToothCharacter* Player) const;
    bool HasClearLanding() const;
    void SetStage(EMCRewardChestStage Next);
    void FinishReward(bool bRequeue);
    void RollMimic();
    bool BeginTeamBoosterChoice(AMCToothCharacter* Player);
    bool TryChooseTeamBooster(AMCToothCharacter* Player,int32 Index);
    void PollTeamBoosterApproach();
    bool BuildTeamBoosterOffer(const UMCPerkComponent* Perks);
    bool ApplyStageBoosterOnce(AMCPlayerState* PlayerState,FName ID,bool bAllowDeferred=false);
    bool ResolveExpiredStageChoice(AMCPlayerState* PlayerState);
    void CaptureStageRecipients();
    bool CanReceiveStageChoice(const AMCToothCharacter* Player) const;
    void ReleaseStageChoice(AMCPlayerState* PlayerState);
    void ReleaseStageChoices();
    bool AreStageChoicesResolved() const;
    void CaptureOpener();
    void ReleaseCaptive(bool FindClearFloor=true);
    void UpdateMimicPresentation(float Age);
    FVector FindMimicReleaseLocation(const AMCToothCharacter* Player) const;
    double ServerNow() const;
    UPROPERTY() TObjectPtr<UDataTable> RewardTable;
    UPROPERTY() TObjectPtr<AMCRewardDropZone> DropZone;
    UPROPERTY(ReplicatedUsing=RefreshPresentation) bool bRewardInitialized=false;
    FTimerHandle ApproachTimer;
    int32 RollSeed=0;
    bool bClaimInProgress=false;
    bool bStageRecipientsCaptured=false;
    TSet<TWeakObjectPtr<AMCPlayerState>> StageRecipients;
    TSet<TWeakObjectPtr<AMCPlayerState>> StageClaims;
    TSet<TWeakObjectPtr<AMCPlayerState>> StageChoicePresented;
    TMap<TWeakObjectPtr<AMCPlayerState>,double> StageChoiceLastSent;
    TMap<TWeakObjectPtr<AMCPlayerState>,TWeakObjectPtr<AMCToothCharacter>> StageChoicePawns;
    bool bReported=false;
    int8 MimicTestOverride=-1;
    UPROPERTY(Replicated) FVector CapturedEntryLocation=FVector::ZeroVector;
    FVector BodyRestLocation=FVector::ZeroVector,LidRestLocation=FVector::ZeroVector;
    FVector BodyRestScale=FVector::OneVector;
    double LandingPresentationStartedAt=-1.;
    EMCRewardChestStage LastPresentationStage=EMCRewardChestStage::Telegraph;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ClaimRingMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ClaimSparkMaterial;
    TMap<TWeakObjectPtr<USceneComponent>,bool> RewardEffectsVisibility;
};
