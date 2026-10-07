#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCBossClot.generated.h"
class UStaticMeshComponent;
class UMCBossMouthAttackComponent;

/** A swept server projectile; peers reconstruct its ballistic flight from the server clock. */
UCLASS()
class MESSCONTROL_API AMCBossClot : public AActor
{
    GENERATED_BODY()
public:
    AMCBossClot();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    static AMCBossClot* Spawn(UMCBossMouthAttackComponent* Source,int32 Serial,FVector Origin,FVector Landing,float FlightSeconds,float Size);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Blob;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Lobe;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Tail;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector LaunchOrigin=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector LaunchVelocity=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly) float Radius=12.f;
    UPROPERTY(Replicated,BlueprintReadOnly) float Gravity=1600.f;
    UPROPERTY(ReplicatedUsing=OnRep_Impact,BlueprintReadOnly) bool bImpacted=false;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector ImpactPoint=FVector::ZeroVector;
    UPROPERTY(Replicated,BlueprintReadOnly) FVector ImpactNormal=FVector::UpVector;
private:
    UFUNCTION() void OnRep_Impact();
    FVector PositionAt(float Age) const;
    double Now() const;
    TWeakObjectPtr<UMCBossMouthAttackComponent> Source;
    int32 SalvoSerial=INDEX_NONE;
    float PreviousAge=0;
    bool bImpactPresented=false;
};
