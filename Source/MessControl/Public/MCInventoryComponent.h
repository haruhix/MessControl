#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
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
public:
    UMCInventoryComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Tools") TSoftObjectPtr<UMCEquipmentProfile> Profile;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") EMCToolSlot Selected=EMCToolSlot::Brush;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") bool bWaterJetUnlocked=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") double SprayReadyAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") double LastSprayAt=-100;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") TObjectPtr<AMCMouthSurface> HealingTarget;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tools") TObjectPtr<class AMCFirePatch> FireTarget;
    FVector SprayAim() const;
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
    bool bSprayEmitting=false;
    double NextSocialSprayAt=0;
    EMCToolSlot Presented=EMCToolSlot::Brush;
    bool bPresentedUpgrade=false;
    bool bPresentedFallback=false;
    void RefreshMesh();
    FVector LocalPickaxeContactTip() const;
    AMCMouthSurface* FindSprayTarget() const;
    class AMCFirePatch* FindFireTarget() const;
    FVector SprayOrigin() const;
    FVector SprayDirection() const;
    void ReactPlayersToSpray();
};
