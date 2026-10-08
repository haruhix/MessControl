#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCNutSpellProjectile.generated.h"

class AMCTongue;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UNiagaraComponent;
class UMaterialInstanceDynamic;

USTRUCT(BlueprintType)
struct FMCNutSpellFlight
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FVector Start=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Target=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) double StartedAt=-1;
    UPROPERTY(BlueprintReadOnly) float Seconds=1;
    UPROPERTY(BlueprintReadOnly) float Radius=28;
};

/** Impact fields travel together, so the terminal effect cannot read an older point or normal. */
USTRUCT(BlueprintType)
struct FMCNutSpellImpact
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bImpacted=false;
    UPROPERTY(BlueprintReadOnly) FVector Point=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Normal=FVector::UpVector;
    UPROPERTY(BlueprintReadOnly) double At=0;
};

/** One terminal server sweep hit; fire, trail and mesh flight are reconstructed on rendering peers. */
UCLASS()
class MESSCONTROL_API AMCNutSpellProjectile : public AActor
{
    GENERATED_BODY()
public:
    AMCNutSpellProjectile();
    static AMCNutSpellProjectile* Spawn(AActor* SourceActor,AMCTongue* Tongue,FVector Start,FVector LockedTarget,float Speed,float Damage,float Radius);
    UFUNCTION(BlueprintCallable,Category="Nut Combat") void Cancel();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated,BlueprintReadOnly) FMCNutSpellFlight Flight;
    UPROPERTY(ReplicatedUsing=PresentImpact,BlueprintReadOnly) FMCNutSpellImpact Impact;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
    UPROPERTY(Replicated,BlueprintReadOnly) TObjectPtr<AActor> SourceActor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Nut;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Flames;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Trail;
    UPROPERTY(BlueprintReadOnly) int32 DamageApplications=0;
private:
    double Now() const;
    FVector PositionAt(float Age) const;
    void ResolveImpact(const FHitResult& Hit);
    UFUNCTION() void PresentImpact();
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FireMID;
    float Damage=18,PreviousAge=0;
    bool bImpactPresented=false,bTrailStarted=false;
};
