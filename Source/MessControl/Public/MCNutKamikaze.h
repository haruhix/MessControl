#pragma once

#include "CoreMinimal.h"
#include "MCNutEnemy.h"
#include "MCNutKamikaze.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimSequence;

/** A killable summoned nut: its fuse starts on spawn, rather than on player contact. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCNutKamikaze : public AMCNutEnemy
{
    GENERATED_BODY()
public:
    AMCNutKamikaze();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Nut Combat|Summon")
    bool ConfigureKamikaze(AMCTongue* OnTongue,UStaticMesh* Mesh,FVector Scale,FMCNutEnemySettings InSettings,
        float FuseSeconds=5.f,float InBlastRadius=230.f,float InBlastDamage=20.f,float InPushSpeed=650.f);

    UFUNCTION(BlueprintPure,Category="Nut Combat|Summon") float GetFuseSecondsRemaining() const;

    UPROPERTY(Replicated,BlueprintReadOnly,Category="Nut Combat|Summon") double FuseStartedAt=-1;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Nut Combat|Summon") double FuseExplodesAt=-1;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Nut Combat|Summon") bool bArmed=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Nut Combat|Summon") bool bExploded=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Nut Combat|Summon") float BlastRadius=230.f;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Nut Combat|Summon") float BlastDamage=20.f;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Nut Combat|Summon") float PushSpeed=650.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Nut Combat|Summon") TObjectPtr<UProceduralMeshComponent> FuseFlame;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Nut Combat|Summon") TObjectPtr<UProceduralMeshComponent> BlastWarning;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|Summon") TSoftObjectPtr<UMaterialInterface> FuseMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|Summon") TSoftObjectPtr<UMaterialInterface> WarningMaterial;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Nut Combat|Summon") TObjectPtr<USkeletalMeshComponent> SummonModel;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|Summon") TSoftObjectPtr<USkeletalMesh> SummonMesh;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|Summon") TSoftObjectPtr<UAnimSequence> RunAnimation;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|Summon") float ModelHeight=116.f;
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|Summon") float ModelYaw=-90.f;
    /** Ground speed represented by one second of the supplied in-place run. */
    UPROPERTY(EditDefaultsOnly,Category="Nut Combat|Summon",meta=(ClampMin="40")) float RunReferenceSpeed=130.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Nut Combat|Summon") float RunPlayRate=0.f;

protected:
    virtual void RefreshPresentation() override;
    virtual void Defeat() override;
private:
    double ServerNow() const;
    void PresentFuse(double Time);
    void Detonate();
    void PresentSummon(double Time,float DeltaSeconds);
    FVector ModelCenter=FVector::ZeroVector,ModelScale=FVector::OneVector,LastModelLocation=FVector::ZeroVector;
    bool bModelMotionInitialized=false;
    double NextNearestTargetAt=0;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FuseMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WarningMID;
};
