#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCPerkVFXProfile.h"
#include "MCVFXCapturedMesh.h"
#include "MCVFXNativeInputs.h"
#include "MCFreezeCapturedGeometry.h"
#include "MCPerkVFXCompositionPlayer.generated.h"

class FMCCoffeeRiverPresentation;
class UNiagaraComponent;
class UNiagaraSystem;
class AMCGameState;
class UMCNiagaraMaterialContextResources;
class UMaterialInstanceDynamic;
class UMCNiagaraProceduralSurfaceResources;

/** Prepared once before commit. Strong references retain all known visuals; playback never loads assets. */
USTRUCT()
struct MESSCONTROL_API FMCPreparedPerkVFXComposition
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UMCVFXComposition> Snapshot;
    UPROPERTY() TArray<TObjectPtr<UNiagaraSystem>> Systems;
    UPROPERTY() FMCPerkVFXSlot Outer;
};

/** Native action values. Negative values leave the artist's own Niagara default intact. */
struct MESSCONTROL_API FMCPerkVFXGameplayValues
{
    FVector Direction=FVector::ZeroVector;
    float Radius=-1.f,Fill=-1.f,Intensity=-1.f,Duration=-1.f;
    FLinearColor Color=FLinearColor::White;
    bool bHasColor=false;
    /** Native LWC link data, never substituted with Direction * Radius. */
    FVector SourcePosition=FVector::ZeroVector,TargetPosition=FVector::ZeroVector;
    bool bHasEndpoints=false,bHasSeed=false;
    uint32 Seed=0;
    /** Distinguishes simultaneous native instances such as independent spray coatings. */
    FGuid EpisodeID;
    /** Actual visible subject, which may differ from the actor that owns the perk. */
    TWeakObjectPtr<USceneComponent> VisualSubject;
    /** Optional actual native socket, sampled after pose evaluation for persistent local states. */
    TWeakObjectPtr<USceneComponent> FrameComponent;
    FName FrameSocket;
    /** Immutable tool pose captured by the accepted Echo hit, not the currently equipped tool. */
    TArray<FMCEchoToolPart> CapturedMeshes;
    bool bCapturedMeshOverhead=false;
    double CapturedContactAt=0;
    /** Peer-local elapsed time of an existing replicated native state; not a gameplay decision or RPC. */
    float LocalStateAgeSeconds=-1.f;
    /** Typed read-only mechanic data; artwork and curves remain in the Niagara asset. */
    FMCVFXNativeInputs NativeInputs;
    /** Local claimed river draw geometry; never serialized as gameplay/replication. */
    TSharedPtr<FMCCoffeeRiverPresentation> RiverSurface;
    /** Local immutable fatal pose only; never put into a replicated receipt. */
    TSharedPtr<const FMCFreezeCapturedGeometry> FreezeCapture;
    /** Observed accepted event boundary; shader age independently uses packet fatal timestamp. */
    double FreezeEventAcceptedAt=-1;
};

/** Independent local cosmetics. Existing action replication dispatches events; particles are never replicated. */
UCLASS(Transient,NotBlueprintable)
class MESSCONTROL_API AMCPerkVFXCompositionPlayer final:public AActor
{
    GENERATED_BODY()
public:
    AMCPerkVFXCompositionPlayer();
    /** Prepared sources and typed overrides are validated before the action commits. No synchronous load here. */
    bool StartPrepared(const FMCPreparedPerkVFXComposition& Prepared,const FTransform& Frame,
        USceneComponent* Subject,UNiagaraComponent* LoopAnchor,bool bHostLoop,const FMCPerkVFXGameplayValues& Values,FString& Error,
        float InitialElapsedSeconds=0.f,AActor* PersistentOwner=nullptr);
    /** Freeze-only preparation, called before the actual location/private-scope barrier.
     * Creates layer components now and simulates the real finite particle count.
     * Shared presentation/catalog/profile remains the authoring entry point. */
    bool PrepareFreezeCaptureLayers(const FMCPreparedPerkVFXComposition& Prepared,USceneComponent* Subject,
        const TSharedPtr<FMCFreezeCapturedGeometry>& Geometry,FGuid Epoch,TConstArrayView<UMaterialInterface*> SlotMaterials,
        UMaterialInterface* Glass,FString& Error);
    /** Loading-only variants copied from each actual NS's typed saved defaults. */
    void RefreshPreparedFreezeMaterialUniforms();
    void GetPreparedFreezeResources(TArray<UObject*>& Out) const;
    bool HasVisibleCapturedFreezeBody() const;
    bool HasRejectedCapturedFreezeLayers() const {return bFreezeCaptureRejected;}
    const FString& GetSurfaceAdmissionError()const{return SurfaceAdmissionError;}
    bool HasAdmittedCapturedFreezeLayers() const;
    const FString& GetFreezeCaptureAdmissionError() const {return FreezeCaptureAdmissionError;}
    /** End the finite captured body only; ordinary gallery rows retain their own tail. */
    void StopCapturedFreezeLayers();
    bool UpdateGameplayValues(const FMCPerkVFXGameplayValues& Values,FString* OutError=nullptr);
    bool ValidateGameplayValues(const FMCPerkVFXGameplayValues& Values,FString& Error) const;
    void UpdateEventFrame(const FTransform& Frame);
    /** Optional location/run ownership. Ordinary active players remain unbound. */
    bool BindToPresentationEpoch(FGuid Epoch,FString& Error);
    void StopEmission(bool bImmediate=false);
    UFUNCTION(BlueprintPure,Category="Perk VFX") bool IsRunning() const {return bRunning;}
    UFUNCTION(BlueprintPure,Category="Perk VFX") int32 GetLiveLayerCount() const;
    UFUNCTION(BlueprintPure,Category="Perk VFX") int32 GetPendingLayerCount() const;
    UFUNCTION(BlueprintPure,Category="Perk VFX") float GetPlaybackTime() const {return Age;}
    UFUNCTION(BlueprintPure,Category="Perk VFX") bool IsStopping() const {return bStopping;}
#if WITH_EDITOR
    /** Read-only editor inspection of the retained accepted pose, never the current equipped tool. False means this is not a managed layer. */
    bool GetCapturedToolContextForEditorInspection(const UNiagaraComponent* Component,
        const TArray<FMCEchoToolPart>*& OutParts,bool& OutOverhead,double& OutContactAt,float& OutLayerStart) const
    {
        OutParts=&RetainedCapturedMeshes;OutOverhead=Gameplay.bCapturedMeshOverhead;
        OutContactAt=Gameplay.CapturedContactAt;OutLayerStart=0;
        for(int32 Index=0;Index<Components.Num();++Index)if(Components[Index].Get()==Component
            && Data.Snapshot&&Data.Snapshot->Layers.IsValidIndex(Index))
        {OutLayerStart=Data.Snapshot->Layers[Index].StartSeconds;return true;}
        return false;
    }
    /** Retained accepted context only; no actor expiry/radius sampling or array-content inspection. */
    const AActor* GetPersistentStateOwnerForEditorInspection(bool& OutPersistent) const
    {OutPersistent=bPersistentState;return PersistentStateOwner.Get();}
    const FMCVFXNativeInput* FindAcceptedNativeInputForEditorInspection(FName Name) const
    {return Gameplay.NativeInputs.Entries.Num()<=FMCVFXNativeInputs::MaxEntries?Gameplay.NativeInputs.Find(Name):nullptr;}
#endif

    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool StartPrewarmedFreeze(const FMCPreparedPerkVFXComposition& Prepared,const FTransform& Frame,USceneComponent* Subject,
        const FMCPerkVFXGameplayValues& Values,float InitialAge,FString& Error);
    void StartLayer(int32 Index);
    bool CreateLayerInactive(int32 Index);
    void ReleaseLayer(int32 Index);
    bool ApplyValues(int32 Index);
    void Advance(float DeltaSeconds);
    UPROPERTY(Transient) FMCPreparedPerkVFXComposition Data;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Components;
    UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> SubjectComponent;
    UPROPERTY(Transient) TWeakObjectPtr<UNiagaraComponent> NativeLoopAnchor;
    UPROPERTY(Transient) TWeakObjectPtr<AMCGameState> PresentationGameState;
    UPROPERTY(Transient) TWeakObjectPtr<AActor> PersistentStateOwner;
    UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> NativeFrameComponent;
    FName NativeFrameSocket;
    FTransform NativeFrameOffset=FTransform::Identity;
    bool bHasNativeFrame=false;
    UPROPERTY(Transient) TArray<FMCEchoToolPart> RetainedCapturedMeshes;
    FGuid PresentationEpoch;
    bool bPresentationEpochBound=false;
    bool bPersistentState=false;
    TArray<bool> Started,Stopped;
    TArray<FMCVFXNativeInputsApplyState> NativeInputStates;
    TArray<bool> ContextLayers;
    UPROPERTY(Transient) TWeakObjectPtr<UMCNiagaraMaterialContextResources> MaterialContextResources;
    UPROPERTY(Transient) TWeakObjectPtr<UMCNiagaraProceduralSurfaceResources> ProceduralSurfaceResources;
    FString PlaybackError;
    FString SurfaceAdmissionError;
    TSet<int32> RejectedSurfaceLayers;
    FMCPerkVFXGameplayValues Gameplay;
    FTransform LastAnchor=FTransform::Identity;
    float Age=0,StopAge=0;
    double NativeClockSampleAt=0;
    bool bRunning=false,bStopping=false,bHostDrivenLoop=false,bSeekToServerAge=false;
    bool PrepareFreezeMaterials(int32 Layer,int32 Slots,TConstArrayView<UMaterialInterface*> ExplicitMaterials,
        UMaterialInterface* ExplicitGlass,TArray<UMaterialInterface*>& Out,UMaterialInterface*& OutGlass,FString& Error);
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FreezeMaterialVariants;
    TArray<int32> FreezeMaterialSourceSlots;
    TArray<FMCPreparedFreezeGeometryLayer> FreezeLayers;
    TArray<bool> FreezeCapturedLayers,FreezeCapturePrepared;
    TArray<FString> FreezeCapturePreparationErrors;
    FGuid FreezePreparedEpoch;
    FString FreezeCaptureAdmissionError;
    bool bFreezeCaptureRejected=false;
    bool bFreezePreparing=false,bFreezePrewarmed=false;
};
