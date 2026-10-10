#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCIceEventVFX.generated.h"

class AMCIceEvent;
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
    bool HasWindVisuals() const;
    bool HasCrystalVisuals() const;
    bool HasRequiredAssets() const;
    int32 GetActiveBurstCount() const {return Bursts.Num();}
    bool IsFinishingPresentation() const {return bFinishing;}
private:
    UNiagaraComponent* MakeLoop(const TCHAR* Name, UNiagaraSystem* System, UMaterialInterface* Material);
    void SetLoop(UNiagaraComponent* Component, bool bEnabled, FVector Position, FRotator Rotation,
        float SpawnRate, float Speed, FVector2D Size, FLinearColor Color);
    void PlayBurst(UNiagaraSystem* System, UMaterialInterface* Material, FVector Position,
        float Speed, FVector Scale, FVector2D Size, FLinearColor Color);
    void RetireBurst(UNiagaraComponent* Component);
    void StopLoops();
    void UpdateWindSheet(const AMCIceEvent& Event, double ServerTime, float Strength, bool bWarning);
    void UpdateFrozenFeet(const AMCIceEvent& Event);
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
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Wind;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Snow;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> CoreCharge;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> VortexWisps;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> VortexSnow;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> CrystalCharges;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> CrystalTrails;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> WarmMotes;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> IcicleCharges;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Bursts;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> WindSheet;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> CrystalFacets;
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
    bool bInitialized=false;
    bool bWasActive=false;
    bool bFinishing=false;
};
