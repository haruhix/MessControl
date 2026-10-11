#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCCombatFeedbackComponent.generated.h"

class UMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** The accepted result of a hit. Cosmetic consumers never apply damage from this snapshot. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCCombatFeedbackHit
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Serial=0;
    UPROPERTY(BlueprintReadOnly) double ServerTime=-1000.;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Source;
    UPROPERTY(BlueprintReadOnly) FVector ContactPoint=FVector::ZeroVector;
    /** World direction away from the damage source. */
    UPROPERTY(BlueprintReadOnly) FVector Direction=FVector::ForwardVector;
    UPROPERTY(BlueprintReadOnly) float HealthDamage=0.f;
    UPROPERTY(BlueprintReadOnly) float ShieldDamage=0.f;
    UPROPERTY(BlueprintReadOnly) bool bShielded=false;
    UPROPERTY(BlueprintReadOnly) bool bFatal=false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMCOnCombatHitPresented,const FMCCombatFeedbackHit&,Hit);

/** One damage presentation path for players, creeps and bosses, independent of their health/AI. */
UCLASS(ClassGroup=(MessControl),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCCombatFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCCombatFeedbackComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;

    /** Call once on authority, after resolving health and shield loss; rejected hits do not produce feedback. */
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Combat|Feedback")
    bool ReportAcceptedHit(AActor* Source,FVector ContactPoint,FVector Direction,float HealthDamage,
        float ShieldDamage=0.f,bool bShielded=false,bool bFatal=false);
    UFUNCTION(BlueprintCallable,Category="Combat|Feedback") void SetVisualTarget(UMeshComponent* Mesh);
    UFUNCTION(BlueprintPure,Category="Combat|Feedback") float GetHitPulse() const;
    UFUNCTION(BlueprintPure,Category="Combat|Feedback") float GetHitFlash() const;

    UPROPERTY(ReplicatedUsing=OnRep_LastHit,BlueprintReadOnly,Category="Combat|Feedback") FMCCombatFeedbackHit LastHit;
    /** Authored animation can consume the same event while retaining its own hit pose. */
    UPROPERTY(BlueprintAssignable,Category="Combat|Feedback") FMCOnCombatHitPresented OnHitPresented;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Combat|Feedback") bool bUseProceduralRecoil=true;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Combat|Feedback") bool bUseBodyOverlay=true;
    /** Baseline comparison for classes that historically had no contact effect. */
    UPROPERTY(EditDefaultsOnly,Category="Combat|Feedback") bool bLegacyHasImpact=true;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UFUNCTION() void OnRep_LastHit();
    double ServerNow() const;
    bool IsLegacy() const;
    float PresentationFlash() const;
    void PresentHit();
    void UpdateOverlay();
    void RestoreOverlay();
    UPROPERTY(Transient) TObjectPtr<UMeshComponent> VisualTarget;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> OverlayBase;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PreviousOverlay;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HitOverlay;
    int32 PresentedSerial=0;
    bool bOverlayInstalled=false;
};
