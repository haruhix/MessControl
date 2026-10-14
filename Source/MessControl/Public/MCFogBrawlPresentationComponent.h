#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCFogBrawlPresentationComponent.generated.h"

class AMCFogBrawlEvent;
class AMCTongue;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;

/** Local smoke, independent of authoritative strike resolution. */
UCLASS()
class MESSCONTROL_API UMCFogBrawlPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCFogBrawlPresentationComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    void ResetSmoke();
    UPROPERTY(EditAnywhere,Category="Smoke",meta=(ClampMin="0",ClampMax="1")) float Density=.12f;
private:
    UPROPERTY() TObjectPtr<AMCFogBrawlEvent> Event;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
    UPROPERTY() TObjectPtr<UPostProcessComponent> Mood;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Billows;
    UPROPERTY() TObjectPtr<UMaterialInterface> SmokeMaterial;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Signals;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Threat;
    UPROPERTY() TObjectPtr<UMaterialInterface> GlowMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> BeaconMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> TellMaterial;
    UPROPERTY() TObjectPtr<UAudioComponent> KnockAudio;
    UPROPERTY() TObjectPtr<UAudioComponent> CueAudio;
    UPROPERTY() TObjectPtr<USoundBase> WindupSound;
    UPROPERTY() TObjectPtr<USoundBase> KnockSound;
    UPROPERTY() TObjectPtr<USoundBase> FinalSound;
    UPROPERTY() TObjectPtr<USoundBase> BeaconSound;
    UPROPERTY() TObjectPtr<USoundBase> ImpactSound;
    UPROPERTY() TObjectPtr<USoundAttenuation> KnockAttenuation;
    TArray<TWeakObjectPtr<UExponentialHeightFogComponent>> SuspendedFog;
    bool bPresenting=false;
    bool bFinalHeard=false,bBeaconHeard=false;
    double PresentedWarning=-1,PresentedImpact=-100,NextKnockAt=0;
    double PresentedSelection=-1;
    int32 PresentedSelectionStep=INDEX_NONE;
    void BeginSmoke();
    void UpdateBillows(double Now,float Weight);
    void UpdateSignals(double Now);
    void UpdateAudio(double Now);
};
