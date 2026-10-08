#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCReactionVFX.generated.h"
UENUM()
enum class EMCReactionEffect : uint8 { Impact, Suction, Yawn, Fire, Stars, Steam, BossWind, BlackClotImpact, WaterStream, WaterShot, WaterImpact, CombatHit, PlayerHit, FoodBreak };

/** Small replicated cosmetic bursts. Geometry is generated locally from the server clock. */
UCLASS()
class MESSCONTROL_API AMCReactionVFX : public AActor
{
    GENERATED_BODY()
public:
    AMCReactionVFX();
    static AMCReactionVFX* Spawn(UWorld* World,FVector Point,EMCReactionEffect Effect,float Seconds=1.4f,float Radius=100,FVector Direction=FVector::ForwardVector,float FlowLength=0);
    /** Server-only presentation of the actual post-protection health loss. Never applies damage. */
    static AMCReactionVFX* SpawnHit(AActor* Victim,FVector Point,FVector Direction,float AppliedDamage,bool bShielded=false);
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UProceduralMeshComponent> Mesh;
    UPROPERTY(Replicated) EMCReactionEffect Effect=EMCReactionEffect::Stars;
    UPROPERTY(Replicated) double StartedAt=0;
    UPROPERTY(Replicated) float Seconds=1.4f;
    UPROPERTY(Replicated) float Radius=100;
    UPROPERTY(Replicated) FVector Direction=FVector::ForwardVector;
    /** Mouth-source vacuum extent, measured from the inlet back across the tongue. */
    UPROPERTY(Replicated) float FlowLength=0;
    UPROPERTY(Replicated) bool bLoop=false;
    UPROPERTY(Replicated,BlueprintReadOnly) float AppliedDamage=0;
    UPROPERTY(Replicated,BlueprintReadOnly) bool bShieldedHit=false;
    /** Reuse the owned dust burst; reflected reference keeps it reachable by cooking. */
    UPROPERTY(EditDefaultsOnly,Category="Feedback") TSoftObjectPtr<class UNiagaraSystem> FoodBreakSystem;
private:
    UPROPERTY() TObjectPtr<class UTextRenderComponent> DamageNumber;
    UPROPERTY() TObjectPtr<class UTextRenderComponent> DamageBackdrop;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> SoftMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FireMaterial;
    UPROPERTY() TObjectPtr<class UPointLightComponent> FireLight;
    bool bFoodBurstPresented=false;
    float PresentedDamage=-1;
    double Now() const;
};
