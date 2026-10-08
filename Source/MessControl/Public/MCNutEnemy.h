#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCNutRainProfile.h"
#include "MCNutEnemy.generated.h"

class AMCFoodActor;
class AMCTongue;
class AMCToothCharacter;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UStaticMesh;

/** Small server-driven enemy supported directly by the live, deforming tongue. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCNutEnemy : public AActor
{
    GENERATED_BODY()
public:
    AMCNutEnemy();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void ConfigureFromFood(const AMCFoodActor* Food,AMCTongue* OnTongue,FMCNutEnemySettings InSettings);
    void ConfigureEnemy(AMCTongue* OnTongue,UStaticMesh* Mesh,FVector Scale,FMCNutEnemySettings InSettings);
    virtual bool IsEncounterAlive() const { return !bDefeated && Health>0 && !IsActorBeingDestroyed(); }
    virtual bool CanReceiveToolHit() const { return IsEncounterAlive(); }
    FVector GetToolTargetPoint(FVector From) const;
    virtual float ReceiveToolDamage(float Damage,AMCToothCharacter* Source);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USphereComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LeftEye;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> RightEye;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly) TObjectPtr<UStaticMesh> NutMesh;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly) FVector MeshScale=FVector::OneVector;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly) FMCNutEnemySettings Settings;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly) float Health=90;
    UPROPERTY(ReplicatedUsing=RefreshPresentation, BlueprintReadOnly) bool bDefeated=false;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AMCToothCharacter> Target;
    UPROPERTY(Replicated, BlueprintReadOnly) double AttackStartedAt=-100;
    UPROPERTY(Replicated, BlueprintReadOnly) double HitAt=-100;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AMCTongue> Tongue;
protected:
    UFUNCTION() virtual void RefreshPresentation();
    bool IsLiveTarget(const AMCToothCharacter* Hero) const;
    bool HasAttackContact(const AMCToothCharacter* Hero) const;
    void Retarget();
    virtual void Defeat();
    void MoveOnTongue(FVector Direction,float Dt);
    void TickPresentation(double Now);
private:
    double NextTargetAt=0,NextAttackAt=0;
    bool bAttackPending=false;
    TWeakObjectPtr<AMCToothCharacter> AttackTarget;
    FVector VisualCenter=FVector::ZeroVector;
    float PhaseOffset=0;
};
