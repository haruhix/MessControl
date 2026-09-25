#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCMouthSurface.generated.h"
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMCToothStatusComponent;
class UMaterialInstanceDynamic;

/** A replaceable stain/ulcer on walkable mouth tissue, independent of the arena mesh. */
UCLASS()
class MESSCONTROL_API AMCMouthSurface : public AActor
{
    GENERATED_BODY()
public:
    AMCMouthSurface();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Area;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCToothStatusComponent> Status;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bUlcer=false;
    UPROPERTY(Replicated, BlueprintReadOnly) float Healing=0;
    UPROPERTY(Replicated) float HealSeconds=15;
    UPROPERTY(Replicated) float DamagePerSecond=.35f;
    UPROPERTY(Replicated) float DisturbDamage=1;
    UPROPERTY(Replicated) bool bDisturbed=false;
    UPROPERTY(Replicated) int32 Batch=0;
    bool IsClean() const;
    void Disturb();
private:
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    float ContactCooldown=0;
};
