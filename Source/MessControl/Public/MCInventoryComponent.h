#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/NetSerialization.h"
#include "MCPerkTypes.h"
#include "MCInventoryComponent.generated.h"

class AMCToothCharacter;
class AMCFoodActor;
class UStaticMeshComponent;
class AMCMouthSurface;
class UNiagaraComponent;

UENUM(BlueprintType)
enum class EMCToolSlot : uint8 { Brush, Pickaxe, Knife, Spray };

/** Artist-replaceable equipment; slot one keeps its cleaning contract after an upgrade. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCEquipmentProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") TSoftObjectPtr<UStaticMesh> PickaxeMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") FTransform PickaxeTransform;
    // Current artist pick: handle along +X, striking point along +Z.
    // A PickaxeTip socket takes priority when a replacement mesh supplies one.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") FVector PickaxeContactTip=FVector(59.53f,0,33.45f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") TSoftObjectPtr<UStaticMesh> KnifeMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") FTransform KnifeTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") TSoftObjectPtr<UStaticMesh> SprayMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") FTransform SprayTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") TSoftObjectPtr<UStaticMesh> WaterJetMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FTransform WaterJetTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") TSoftObjectPtr<UStaticMesh> MeshaBrushMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FTransform MeshaBrushTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FVector MeshaBrushContact=FVector(67,0,-9);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FVector MeshaBrushSupportGrip=FVector(-35,0,5);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FRotator MeshaBrushSupportRotation;
    // Tool centre/orientation in character space; size comes from its attachment.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Idle") FTransform MeshaBrushIdlePose;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") TSoftObjectPtr<UStaticMesh> ChainsawMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FTransform ChainsawTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FVector ChainsawSupportGrip=FVector(-36,0,40);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FRotator ChainsawSupportRotation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Idle") FTransform ChainsawIdlePose;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") TSoftObjectPtr<UStaticMesh> BufferMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FTransform BufferTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FVector BufferSupportGrip=FVector(-28,0,30);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FRotator BufferSupportRotation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Idle") FTransform BufferIdlePose;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FTransform BufferWorkPose;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FTransform BufferWorkTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FVector BufferWorkSupportGrip;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FRotator BufferWorkSupportRotation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") TSoftObjectPtr<UStaticMesh> WatergunMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FTransform WatergunTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FVector WatergunSupportGrip=FVector(20,0,-12);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Grip") FRotator WatergunSupportRotation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Idle") FTransform WatergunIdlePose;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FTransform WatergunWorkPose;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FTransform WatergunWorkTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FVector WatergunWorkSupportGrip;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") FRotator WatergunWorkSupportRotation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade|Work") bool bUseWorkingToolPoses=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FVector WatergunNozzle=FVector(102,0,12);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") float WatergunCareReach=1000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") float WatergunChargeSeconds=1.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") float WatergunShotCooldown=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") TSoftObjectPtr<UMaterialInterface> UpgradeFallbackMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="0.1")) float PickaxeDamage=40;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="0.1")) float KnifeDamage=25;
    // Retained for loading old assets; treatment now follows held input without a cooldown.
    UPROPERTY() float SprayCooldown=8;
    UPROPERTY() float NumbSeconds=10;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="10")) float SprayReach=235;
};

UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCInventoryComponent : public UActorComponent
{
    GENERATED_BODY()
#if !UE_BUILD_SHIPPING
    friend void MCTickSprayNetworkValidation(UWorld* World);
#endif
#if WITH_DEV_AUTOMATION_TESTS
    friend class FMCToolBoosterMechanics;
    friend class FMCSprayProtection;
#endif
public:
    UMCInventoryComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Tools") TSoftObjectPtr<UMCEquipmentProfile> Profile;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") EMCToolSlot Selected=EMCToolSlot::Brush;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") bool bWaterJetUnlocked=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") double SprayReadyAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") double LastSprayAt=-100;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") TObjectPtr<AMCMouthSurface> HealingTarget;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") TObjectPtr<class AMCFirePatch> FireTarget;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools|Watergun") bool bPressureMode=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools|Watergun") bool bChargingWater=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools|Watergun") double WaterChargeStartedAt=-100;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools|Watergun") double WaterShotReadyAt=0;
    UFUNCTION(BlueprintPure,Category="Tools") bool HasUpgrade(EMCToolUpgrade Kind) const;
    UFUNCTION(BlueprintPure,Category="Tools") bool IsChainsawRunning() const;
    UFUNCTION(BlueprintPure,Category="Tools") bool IsUsingBuffer() const;
    UFUNCTION(BlueprintPure,Category="Tools") bool IsUsingWatergun() const;
    UFUNCTION(BlueprintPure,Category="Tools") EMCToolUpgrade SelectedUpgrade() const;
    bool UpgradeIdleGrip(FTransform& RightHandWorld) const;
    bool UpgradeWorkToolPose(FTransform& ToolWorld) const;
    bool WatergunHandGoal(FTransform& RightHandWorld) const;
    UFUNCTION(BlueprintPure,Category="Tools|Watergun") float WaterRecoil() const;
    FQuat ToolHandRotation(const FQuat& ToolWorldRotation) const;
    bool UpgradeSupportGrip(const FTransform& RightHandWorld,FTransform& LeftHandWorld) const;
    UFUNCTION(BlueprintPure,Category="Tools|Watergun") float WaterChargeFraction() const;
    /** Camera-centre target shared by spray, watergun and their working poses. */
    UFUNCTION(BlueprintPure,Category="Tools|Watergun") FVector WatergunAimPoint() const;
    void UpdateSprayAim(bool Commit=false);
    float MovementMultiplier() const;
    float CleaningSpeedMultiplier() const;
    float CleaningRadius(float DirtRadius=36.f) const;
    FVector BrushContactLocal() const;
    float SprayReach() const;
    void HandleChainsawCollision(AActor* Other,const FHitResult& Hit);
    void CancelUpgradeUse();
    UFUNCTION(BlueprintPure,Category="Tools") FVector SprayAim() const;
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Tools") void ServerSelect(EMCToolSlot Slot);
    UFUNCTION(Server,Reliable,BlueprintCallable,Category="Tools") void ServerSpray();
    // Called by the authoritative upgrade/shop system. Clients cannot grant upgrades.
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Tools") void UnlockWaterJet();
    bool IsCleaningTool() const { return Selected==EMCToolSlot::Brush; }
    bool CanBreak(const AMCFoodActor* Food) const;
    float Damage() const;
    float CooldownSeconds() const;
    float SpraySecondsLeft() const;
    float SwingDuration() const;
    float SwingContactTime() const;
    // Converts elapsed server seconds to the authored swing timeline.
    static float SwingPlayRate(EMCToolSlot Slot);
    static float SwingAngle(EMCToolSlot Slot,float Elapsed);
    static FVector SwingOffset(EMCToolSlot Slot,float Elapsed);
    // Mesh visibility, hand presentation and collision correction share this
    // context so a hidden tool cannot displace traversal or grip contacts.
    bool ShouldPresentTool() const;
    FVector ConstrainPickaxeGrip(const FTransform& WristWorld) const;
    bool CalculusHandGoal(FTransform& HandWorld,float& Blend) const;
    FVector PickaxeContactTip() const;
    FString ToolName() const;
private:
    double Now() const;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<UMCEquipmentProfile> Settings;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Tool;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Detail;
    UPROPERTY() TObjectPtr<UNiagaraComponent> SprayMist;
    UPROPERTY() TObjectPtr<UNiagaraComponent> WaterMist;
    bool bSprayEmitting=false;
    bool bWaterEmitting=false;
    float WorkingGripAlpha=0;
    double NextSocialSprayAt=0;
    UPROPERTY(Replicated) FVector_NetQuantize SprayViewOrigin;
    UPROPERTY(Replicated) FVector_NetQuantizeNormal SprayViewDirection=FVector::ForwardVector;
    UPROPERTY(Replicated) bool bHasSprayView=false;
    double NextSprayViewAt=0;
    bool LocalSprayView(FVector& Origin,FVector& Direction) const;
    void StoreSprayView(FVector Origin,FVector Direction);
    UFUNCTION(Server,Unreliable) void ServerUpdateSprayView(FVector_NetQuantize Origin,FVector_NetQuantizeNormal Direction);
    UFUNCTION(Server,Reliable) void ServerCommitSprayView(FVector_NetQuantize Origin,FVector_NetQuantizeNormal Direction);
    bool SprayTargetDistance(AActor* Target,FVector Point,float Radius,float& Distance) const;
    EMCToolSlot Presented=EMCToolSlot::Brush;
    bool bPresentedUpgrade=false;
    bool bPresentedFallback=false;
    uint8 PresentedUpgradeMask=0;
    UPROPERTY() TObjectPtr<UStaticMesh> OriginalBrushMesh;
    FTransform OriginalBrushTransform;
    uint16 SawMotionId=0;
    bool bSawMotionActive=false;
    double NextSawContactAt=0,SawStunEndsAt=-100;
    TMap<TWeakObjectPtr<AActor>,double> SawContacts;
    void TickUpgrades(float Dt);
    void TickChainsaw(float Dt);
    void SawContact();
    void FireChargedWater(float Charge);
    bool TraceWaterShot(float Charge,const FVector& Origin,FVector& End,FHitResult& Hit) const;
    FTransform UpgradeAttachment() const;
    uint8 UpgradeMask() const;
    void RefreshMesh();
    FVector LocalPickaxeContactTip() const;
    AMCMouthSurface* FindSprayTarget() const;
    class AMCFirePatch* FindFireTarget() const;
    FVector SprayOrigin() const;
    FVector SprayDirection() const;
    void ReactPlayersToSpray();
};
