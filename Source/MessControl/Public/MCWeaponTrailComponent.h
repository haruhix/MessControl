#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCWeaponTrailComponent.generated.h"

class AMCToothCharacter;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMCWorldPresentationComponent;

struct FMCWeaponTrailSample
{
    FVector Base,Tip;
    double At=0;
};

/** Cosmetic blade-aligned history. The existing swing multicast starts it on rendering peers. */
UCLASS()
class MESSCONTROL_API UMCWeaponTrailComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCWeaponTrailComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    void BeginSwing();
    /** Same accepted native multicast, no new transport or interaction loading. */
    void PublishAcceptedSwing(FName SourceID,double StartedAt,uint8 VisualStep);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditDefaultsOnly,Category="Weapon trail") TSoftObjectPtr<UMaterialInterface> TrailMaterial;
    UPROPERTY(EditDefaultsOnly,Category="Weapon trail",meta=(ClampMin="0.03",ClampMax="0.25")) float HistorySeconds=.14f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Weapon trail") TObjectPtr<UProceduralMeshComponent> Ribbon;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Weapon trail") int32 ActiveSamples=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Weapon trail") int32 RenderedVertices=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Weapon trail") int32 PresentedSwings=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Weapon trail") bool bEmitting=false;
private:
    UPROPERTY() TObjectPtr<AMCToothCharacter> Hero;
    TArray<FMCWeaponTrailSample> Samples;
    FVector Origin=FVector::ZeroVector;
    uint8 SwingTool=255;
    void Upload(double Now);
    void PublishComposerState(bool bActive,float PoseDelta,bool bHaveSpan,FVector Base,FVector Tip,bool bRenderAllowed);
    void StopComposerState();
    UPROPERTY(Transient) TObjectPtr<UMCWorldPresentationComponent> Composer;
    FName PresentationSource;
    FGuid PresentationEpisode;
    double AcceptedAt=-100.;
    uint8 AcceptedStep=0;
    int32 KatanaStroke=INDEX_NONE;
    int32 PoseBreakSerial=0;
    bool bPublished=false,bHasPreviousPose=false,bNativeTrail=true;
    FVector PreviousBase=FVector::ZeroVector,PreviousTip=FVector::ZeroVector;

};
