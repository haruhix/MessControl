#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCFreezePoseCapture.h"
#include "RenderCommandFence.h"
#include "MCFreezeDeathVFX.generated.h"
class AMCToothCharacter;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USkeletalMesh;
class USkeletalMeshComponent;

/** Local geometry driven by the character's replicated fatal-freeze timestamp. */
UCLASS()
class MESSCONTROL_API AMCFreezeDeathVFX : public AActor
{
    GENERATED_BODY()
public:
    AMCFreezeDeathVFX();
    static AMCFreezeDeathVFX* SpawnLocal(AMCToothCharacter* Hero,double StartedAt);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** Per-real-hero preparation before readiness. Does not start the fatal state. */
    bool PrepareNative(AMCToothCharacter* Hero,FString& Error);
    bool PrepareNiagara(AMCToothCharacter* Hero,FGuid Epoch,const FMCPreparedPerkVFXComposition& Hold,const FMCPreparedPerkVFXComposition& Shatter,
        UMaterialInterface* BodyMaster,UMaterialInterface* BagMaster,UMaterialInterface* GlassMaster,FString& Error);
    bool CommitPrepared(AMCToothCharacter* Hero,double At);
    /** Readiness observation only; no capture, simulation, load or pose refresh. */
    bool ValidatePreparation(const AMCToothCharacter* Hero,FGuid Epoch,FString& Error) const;
    bool IsCommitted() const {return bCommitted;}
    bool HasAcceptedShatter() const {return bShatterAccepted;}
    void GetPreparedResources(TArray<UObject*>& Out) const;
    bool HasVisibleBody() const {return bNativeVisible||bNiagaraVisible;}
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UProceduralMeshComponent> IceCube;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<TObjectPtr<UProceduralMeshComponent>> BodyFragments;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<TObjectPtr<UProceduralMeshComponent>> IceFragments;
private:
    void Capture(AMCToothCharacter* Hero);
    void RefreshPreparedUniforms();
    void RenderNativeSnapshot(const MCFreezeDeathSnapshot::FBuffer& Snapshot);
    void StopPreparedNiagara();
    bool StartPreparedPhase(bool bShatter,double Now,FString& Error);
    void DispatchAcceptedPhaseAudio(bool bShatter,double Now);
    EMCPerkVFXSlotMode GetPhaseMode(bool bShatter) const;
    bool PhaseHasCapture(bool bShatter,float EventAge=-1.f) const;
    void ReadPreparedPhasePolicy();
    bool EnsureNativeGeometry();
    UProceduralMeshComponent* MakePart(FName Name);
    UPROPERTY() TObjectPtr<UMaterialInterface> GlassMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> BodyMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> BagMaterial;
    UPROPERTY() TObjectPtr<AMCToothCharacter> Source;
    struct FShard { FVector Home,Velocity,Spin; float Floor=0; };
    TArray<FShard> Motion;
    double StartedAt=0;
    float HalfSize=100;
    bool bShattered=false;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> NativeGlass;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> NiagaraGlass;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> NativeMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> NiagaraMaterials;
    UPROPERTY(Transient) TObjectPtr<AMCPerkVFXCompositionPlayer> HoldPlayer;
    UPROPERTY(Transient) TObjectPtr<AMCPerkVFXCompositionPlayer> ShatterPlayer;
    UPROPERTY(Transient) FMCPreparedPerkVFXComposition HoldDefinition;
    UPROPERTY(Transient) FMCPreparedPerkVFXComposition ShatterDefinition;
    FMCFreezePoseCapture PoseCapture;
    TSharedPtr<FMCFreezeCapturedGeometry> Geometry;
    FGuid PreparedEpoch;
    TWeakObjectPtr<USkeletalMeshComponent> PreparedSubject;
    TWeakObjectPtr<USkeletalMesh> PreparedAppearance;
    TArray<TWeakObjectPtr<UMaterialInterface>> PreparedSourceMaterials;
    FRenderCommandFence NativeRenderFence;
    uint64 NativeGeometryWrittenFrame=0,NativeFenceFrame=0;
    double ShatterAcceptedAt=-1;
    FString LastPresentationError;
    bool bNativePrepared=false,bPosePrepared=false,bCommitted=false,bNiagaraPrepared=false;
    bool bNiagaraVisible=false,bShatterAccepted=false,bNativePending=false,bShatterAudioIssued=false;
    bool bFreezePolicyPrepared=false,bOwnsHeroHide=false,bNaturalPresentationEnd=false;
    bool CaptureFallback[2]={false,false};
    bool bNativeGeometryWritten=false,bNativeVisible=false,bNativeFenceQueued=false;
};
