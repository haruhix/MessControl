#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MCNiagaraProceduralSurfaceResources.generated.h"
class UNiagaraComponent;
class UNiagaraSystem;
class UProceduralMeshComponent;
class UMCNiagaraProceduralSurfaceComponent;
class UMaterialInterface;
class FRenderCommandFence;
class AActor;

/** Location/load scoped preparation owned by the existing preload subsystem.
 * Saved NS templates declare finite concurrent reserves. No catalog/global pool,
 * late actor spawn limit, gameplay actor, or first-event resource allocation. */
UCLASS(Transient,NotBlueprintable)
class MESSCONTROL_API UMCNiagaraProceduralSurfaceResources final:public UObject
{
    GENERATED_BODY()
public:
    UMCNiagaraProceduralSurfaceResources();
    virtual ~UMCNiagaraProceduralSurfaceResources()override;
    virtual UWorld* GetWorld()const override;
    static bool Uses(const UNiagaraSystem* System);
    static FName SourceFor(const UNiagaraSystem* System);
    bool Prepare(UWorld* World,FGuid Epoch,const TArray<UObject*>& Loaded,int32 TongueTriangleBound,FString& Error);
    bool Poll(bool& Waiting,FString& Error);
    bool IsReadyForPlayback(const UWorld* World)const;
    UNiagaraComponent* Acquire(UNiagaraSystem* System,UProceduralMeshComponent* Subject,FName SourceID,FGuid Episode,FString& Error);
    bool Owns(const UNiagaraComponent* Component)const;
    void UpdateRendererEligibility(UNiagaraComponent* Component,bool Enabled);
    bool Release(UNiagaraComponent* Component);
    bool HasAcceptedDraw(UProceduralMeshComponent* Subject,FName SourceID,FGuid Episode=FGuid())const;
    void GetResources(TArray<UObject*>& Out)const;
    UFUNCTION(BlueprintPure,Category="Surface|Inspection") FString GetPreparationStateJson()const;
    void Reset();
private:
    struct FLease
    {
        TWeakObjectPtr<UNiagaraSystem> System;
        TWeakObjectPtr<UNiagaraComponent> Component;
        TWeakObjectPtr<AActor> Host;
        TWeakObjectPtr<UMCNiagaraProceduralSurfaceComponent> Draw;
        TWeakObjectPtr<UProceduralMeshComponent> Subject;
        FName SourceID;
        FGuid Episode;
        int32 Capacity=0;
        bool bClaimed=false;
    };
    TArray<FLease> Leases;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Retained;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCNiagaraProceduralSurfaceComponent>> PreparedDraws;
    TWeakObjectPtr<UWorld> PreparedWorld;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> PresentationActors;
    TArray<TWeakObjectPtr<UNiagaraSystem>> PreparedSystems;
    FGuid PreparedEpoch;
    uint64 SubmittedFrame=0;
    TUniquePtr<FRenderCommandFence> Fence;
    bool bReady=false;
    bool bPreparationSucceeded=false;
    double PreparationStarted=0;
    bool IsCurrent()const;
};
