#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "MCInventoryComponent.generated.h"

class AMCToothCharacter;
class AMCFoodActor;
class UStaticMeshComponent;

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
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") TSoftObjectPtr<UStaticMesh> KnifeMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") FTransform KnifeTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") TSoftObjectPtr<UStaticMesh> SprayMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tools") FTransform SprayTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") TSoftObjectPtr<UStaticMesh> WaterJetMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Upgrade") FTransform WaterJetTransform;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="0.1")) float PickaxeDamage=40;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="0.1")) float KnifeDamage=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="1")) float SprayCooldown=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="1")) float NumbSeconds=10;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Balance",meta=(ClampMin="10")) float SprayReach=235;
};

UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCInventoryComponent : public UActorComponent
{
    GENERATED_BODY()
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
    FString ToolName() const;
private:
    double Now() const;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    UPROPERTY() TObjectPtr<UMCEquipmentProfile> Settings;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Tool;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Detail;
    EMCToolSlot Presented=EMCToolSlot::Brush;
    bool bPresentedUpgrade=false;
    void RefreshMesh();
};
