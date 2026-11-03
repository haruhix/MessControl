#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCIceEventVFX.generated.h"

class AMCIceEvent;
class UMCWorldPresentationComponent;
struct FMCPerkVFXGameplayValues;
class AMCToothCharacter;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UTextRenderComponent;

/** Client-local effects inferred from the winter event's replicated state. */
UCLASS(NotBlueprintable, Transient)
class MESSCONTROL_API AMCIceEventVFX : public AActor
{
    GENERATED_BODY()
public:
    AMCIceEventVFX();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void UpdateFromEvent(const AMCIceEvent& Event, float DeltaSeconds, double ServerTime);
    void FinishPresentation(FVector CoreFloorPoint);
    /** Actual peer-local native observations; no manufactured preview timeline. */
    void GetObservedComposerEvents(TMap<FName,uint64>& Out) const {Out=ComposerEventSerials;}
    UMCWorldPresentationComponent* GetComposerPresentation() const {return ComposerPresentation;}

    /** Explicit location preparation creates native bounded components only, never an Ice gameplay event. */
    bool PrepareForLocation(FGuid Token);
    bool ClaimPreparedPresentation(AMCIceEvent* Source,FGuid Token);
    bool CancelPresentationFor(const AMCIceEvent* Source,FGuid Token);
    void GetLocationPreparedResources(TArray<UObject*>& Out) const;
    bool HasWindVisuals() const;
    bool HasCrystalVisuals() const;
    bool HasRequiredAssets() const;
    int32 GetActiveBurstCount() const {return Bursts.Num();}
    bool IsFinishingPresentation() const {return bFinishing;}
private:
    friend class FMCPrewarmedIcePresentationOwnership;
    void EnsureFrozenFootCapacity(int32 PlayerCount);
    bool bPreparedForLocation=false,bPreparedClaimed=false;
    FGuid PreparedClaimToken;
    TWeakObjectPtr<AMCIceEvent> PreparedClaimSource;
    struct FComposerLoopCall
    {
        TWeakObjectPtr<UNiagaraComponent> Component;
        bool bEnabled=false;
        FVector Position=FVector::ZeroVector;
        FRotator Rotation=FRotator::ZeroRotator;
        float Rate=0,Speed=0,Progress=0,SampleAge=0,PhaseRadius=0;
        uint32 ActivationEpoch=0;
        FVector2D Size=FVector2D::ZeroVector;
        FLinearColor Color=FLinearColor::White;
        int32 Family=0,NativeState=0,Ordinal=0;
    };
    struct FComposerLoopMemory {FComposerLoopCall Last;bool bHasFrame=false,bEnabled=false;uint32 Epoch=0;};
    TMap<TWeakObjectPtr<UNiagaraComponent>,FComposerLoopMemory> ComposerLoopMemory;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMCWorldPresentationComponent> ComposerPresentation;
    TMap<FName,TArray<FComposerLoopCall>> ComposerLoopCalls;
    TMap<FName,FGuid> ComposerLoopEpisodes;
    TMap<FName,double> ComposerLoopStarted;
    TMap<FName,uint64> ComposerEventSerials;
    double ComposerServerTime=0,ComposerEventAge=0;
    float ComposerPhaseRadius=0;
    FGuid ComposerSheetEpisodes[2];
    double ComposerSheetStarted[2]={0,0};
    TSet<FName> ComposerLoopOverflow;
    bool UseOriginalSheet(FName Event,FMCPerkVFXGameplayValues& Values,bool bActive);
    bool bCollectComposerLoops=false;
    FName LastComposerBurstEvent;
    bool bLastComposerBurstUsesOriginal=true;
    FName IdentifyComposerLoop(const UNiagaraComponent* Component,int32& Family,int32& Ordinal) const;
    void AdvanceOriginalLoop(FComposerLoopCall& Call);
    void ApplyOriginalLoop(const FComposerLoopCall& Call,bool bUseOriginal);
    void FlushComposerLoops();
    bool UseOriginalBurst(FName Event,FVector Position);
    void ObserveComposerEvent(FName Event);

    UNiagaraComponent* MakeLoop(const TCHAR* Name, UNiagaraSystem* System, UMaterialInterface* Material);
    void SetLoop(UNiagaraComponent* Component, bool bEnabled, FVector Position, FRotator Rotation,
        float SpawnRate, float Speed, FVector2D Size, FLinearColor Color,float NativeProgress=0,int32 NativeState=0);
    void PlayBurst(UNiagaraSystem* System, UMaterialInterface* Material, FVector Position,
        float Speed, FVector Scale, FVector2D Size, FLinearColor Color,FName ComposerEvent=NAME_None,bool bNewBoundary=true);
    void RetireBurst(UNiagaraComponent* Component);
    void StopLoops();
    void UpdateWindSheet(const AMCIceEvent& Event, double ServerTime, float Strength, bool bWarning);
    void UpdateColdThronePresentation(const AMCIceEvent& Event, float DeltaSeconds, double ServerTime);
    void UpdateBombPresentation(const AMCIceEvent& Event, double ServerTime);
    void UpdateFrozenFeet(const AMCIceEvent& Event);
    void UpdateComposerFacets(FName Event,const TArray<TObjectPtr<UStaticMeshComponent>>& Facets,int32 Capacity);
    TMap<FName,FGuid> ComposerFacetEpisodes;
    TMap<FName,double> ComposerFacetStarted;
    void UpdateHealthLabels(const AMCIceEvent& Event, double ServerTime);
    UFUNCTION() void OnBurstFinished(UNiagaraComponent* Component);

    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UNiagaraSystem> WindSystem;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UNiagaraSystem> SnowSystem;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UNiagaraSystem> MistSystem;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UNiagaraSystem> ShardSystem;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UNiagaraSystem> ChargeSystem;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UMaterialInterface> WindSheetMaterial;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UMaterialInterface> WispMaterial;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UMaterialInterface> SnowMaterial;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UMaterialInterface> MistMaterial;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UMaterialInterface> ChargeMaterial;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UStaticMesh> CrystalMesh;
    UPROPERTY(EditDefaultsOnly, Category="VFX") TSoftObjectPtr<UMaterialInterface> CrystalMaterial;
    /** Soft paths identify cooked assets; these strong references own their loaded lifetime. */
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> LoadedAssets;
    /** One original tongue-frost MID/factory, prepared before the location barrier. */
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PreparedFrostSurfaceMID;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> PreparedFrostSurfaceFactory;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Wind;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Snow;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> CoreCharge;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> BombCharge;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> WinterWisps;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> WinterSnow;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> VortexWisps;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> VortexSnow;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> CrystalCharges;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> CrystalTrails;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> WarmMotes;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> IcicleCharges;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Bursts;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> WindSheet;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> CrystalFacets;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BombFacets;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FootFacets;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> FootMotes;
    UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> CoreHealthLabel;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> CrystalHealthLabels;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SheetMID;

    struct FCrystalState { FVector Anchor; float Health=0; int32 Id=0; bool bLanded=false; };
    TMap<int32, FCrystalState> PreviousCrystals;
    TMap<TWeakObjectPtr<AMCToothCharacter>, float> PreviousFeet;
    TMap<TWeakObjectPtr<UNiagaraComponent>, double> BurstDeadlines;
    FVector LastCorePoint=FVector::ZeroVector;
    float PreviousCoreHealth=0;
    double LastNovaImpact=0;
    double BlastStartedAt=-100;
    double LastSheetUpdate=-100;
    double LastCrystalUpdate=-100;
    int32 LastImpactStrikeId=0;
    int32 LastBombImpactSerial=0;
    float WinterStrength=0;
    bool bWasGlobalCold=false;
    bool bInitialized=false;
    bool bWasActive=false;
    bool bFinishing=false;
};
