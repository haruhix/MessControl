#pragma once

#include "CoreMinimal.h"
#include "MCActiveSkillAction.h"
#include "MCActiveSkillPerkEffect.h"
#include "MCRocketSkillAction.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EMCRocketSkillPhase : uint8 { Prepared, Windup, Flying, Detonated };

/** One committed projectile; source/last-charge changes cannot erase its accepted flight. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCRocketSkillAction : public AMCActiveSkillAction
{
    GENERATED_BODY()
public:
    AMCRocketSkillAction();
    virtual bool CanStart() const override;
    virtual void StartCommitted() override;
    virtual void Cancel(EMCActiveSkillEndReason Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Rocket") TObjectPtr<USphereComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Rocket") TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Rocket") TObjectPtr<UStaticMeshComponent> ProjectileMesh;
    /** Absolute caster-held frame; never inherits the flying projectile transform. */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Rocket|Presentation") TObjectPtr<USceneComponent> LauncherRoot;
    UPROPERTY(EditDefaultsOnly,Category="Rocket|Presentation",meta=(ClampMin="0.15",ClampMax="1.2")) float LauncherRecoverySeconds=.42f;
    /** Complete mesh mount relative to the pawn capsule, measured from the artist's posed guide. */
    UPROPERTY(EditDefaultsOnly,Category="Rocket|Presentation") FTransform LauncherMount=FTransform::Identity;
    UPROPERTY(EditDefaultsOnly,Category="Rocket|Presentation") FVector LauncherMuzzleLocal=FVector(80.092,-3.8078,36.0869);
    UPROPERTY(EditDefaultsOnly,Category="Rocket|Presentation") FVector LauncherForwardLocal=FVector::ForwardVector;
    UFUNCTION(BlueprintPure,Category="Rocket|Presentation") bool IsLauncherHeld() const;
    UFUNCTION(BlueprintPure,Category="Rocket|Presentation") FVector GetLauncherMuzzlePoint() const;
    UFUNCTION(BlueprintPure,Category="Rocket|Presentation") FVector GetLauncherForward() const;
    /** Separate from the held launcher; flight Niagara also follows this projectile actor. */
    UPROPERTY(EditDefaultsOnly,Category="Rocket|Presentation") TSoftObjectPtr<UStaticMesh> ProjectileAsset;
    UPROPERTY(EditDefaultsOnly,Category="Rocket|Presentation") FTransform ProjectileTransform=FTransform::Identity;
    UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Rocket") EMCRocketSkillPhase Phase=EMCRocketSkillPhase::Prepared;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rocket") double FlightEndsAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Rocket") int32 DetonationCount=0;
protected:
    virtual void BeginPlay() override;
    virtual void OnFinished(EMCActiveSkillEndReason Reason) override;
private:
    UFUNCTION() void OnRep_Phase();
    UFUNCTION() void OnImpact(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,
        FVector NormalImpulse,const FHitResult& Hit);
    UFUNCTION() void OnProjectileStopped(const FHitResult& Hit);
    FVector MeshLocalMuzzle() const;
    FTransform ReadyLauncherPose(FVector Direction) const;
    float FlightSeconds() const;
    float WindupSeconds() const;
    bool ReadLaunch(FVector& Origin,FVector& Direction,FHitResult* Block=nullptr,FTransform* PropWorld=nullptr) const;
    void Launch(bool bRefreshAim);
    void Detonate(FVector Point,FVector Normal);
    void UpdateHeldLauncher();
    FVector PreparedOrigin=FVector::ZeroVector;
    FVector PreparedDirection=FVector::ForwardVector;
    double LaunchAt=0;
    double FinishAt=0;
    UPROPERTY(Replicated) FTransform InitialDrawPropActorLocal=FTransform::Identity;
    UPROPERTY(Replicated) bool bHasDrawStart=false;
};

UCLASS()
class MESSCONTROL_API UMCRocketSkillPerkEffect : public UMCActiveSkillPerkEffect
{
    GENERATED_BODY()
public:
    UMCRocketSkillPerkEffect();
};
