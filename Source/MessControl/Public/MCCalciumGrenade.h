#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCCalciumGrenade.generated.h"

class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInterface;

/** A short underfoot toss. Only its living owner can receive the calcium treatment. */
UCLASS()
class MESSCONTROL_API AMCCalciumGrenade : public AActor
{
    GENERATED_BODY()
public:
    AMCCalciumGrenade();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Capsule;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UNiagaraComponent> Mist;
    UPROPERTY(EditDefaultsOnly,Category="Calcium") TSoftObjectPtr<UNiagaraSystem> MistSystem;
    UPROPERTY(EditDefaultsOnly,Category="Calcium") TSoftObjectPtr<UMaterialInterface> CapsuleMaterial;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Calcium") double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Calcium") float HealAmount=35.f;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Calcium") bool bDetonated=false;
private:
    double Now() const;
    void Detonate();
    bool bMistPresented=false;
};
