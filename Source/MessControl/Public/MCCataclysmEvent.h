#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCCataclysmEvent.generated.h"

class UMCDayPlan;
class AMCTongue;
class AMCCoffeeFlood;
class AMCCataclysmRaft;
class AMCCataclysmJelly;
class AMCCataclysmDebris;
class AMCMouthSurface;
class AMCArenaTooth;
class UMCToothStatusComponent;

UENUM(BlueprintType)
enum class EMCCataclysmVariant : uint8 { Random, Rafts, Jellies };

UENUM(BlueprintType)
enum class EMCCataclysmPhase : uint8 { Dormant, Warning, FirstWave, BranchWarning, Flooding, JellyFight, Cleanup, Complete, Stopped };

/** One drink wave commits to one encounter; only its actual consequences gate completion. */
UCLASS()
class MESSCONTROL_API AMCCataclysmEvent : public AActor
{
    GENERATED_BODY()
public:
    AMCCataclysmEvent();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    bool Start(const UMCDayPlan* Plan, EMCCataclysmVariant Choice=EMCCataclysmVariant::Random, int32 Batch=0);
    /** Raft gameplay is retained for later work, but excluded from live events. */
    static bool IsVariantEnabled(EMCCataclysmVariant Choice) { return Choice!=EMCCataclysmVariant::Rafts; }
    void Stop(bool bClearEventDirt=false);
    void RegisterDirtyPatch(FVector Center, float Radius, bool bDeathBurst);
    // Native authoritative gel-pen path, clipped to reachable tongue gameplay zones.
    void RegisterPaintStroke(FVector From,FVector To,float Radius);
    // An owned moving jelly already validated a larger walking footprint.
    void RegisterJellyPaintStroke(const AMCCataclysmJelly* Source,FVector From,FVector To,float Radius);
    static AMCCataclysmEvent* FindActive(UWorld* World);
    bool IsRunning() const;
    bool IsComplete() const { return Phase==EMCCataclysmPhase::Complete; }
    FString Title() const;
    FString Instruction() const;
    float HazardSecondsRemaining() const;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cataclysm",meta=(ClampMin=".5",ClampMax="8")) float WarningSeconds=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cataclysm",meta=(ClampMin="5",ClampMax="120")) float FloodHoldSeconds=35.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cataclysm",meta=(ClampMin="1",ClampMax="12")) float FloodFillSeconds=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cataclysm",meta=(ClampMin="1",ClampMax="12")) float FloodDrainSeconds=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cataclysm",meta=(ClampMin="80",ClampMax="1000")) float JellyHealth=360.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cataclysm",meta=(ClampMin="4",ClampMax="32")) int32 MaxDirtPatches=16;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cataclysm|Paint",meta=(ClampMin="4",ClampMax="24")) int32 MaxPaintTiles=16;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 PaintTilesCreated=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 PaintTileCount=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 PaintStrokesAccepted=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 PaintCoverageCells=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 PaintWetCells=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") float PaintDistanceCm=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 DeathPaintBursts=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 DeathPaintCoverageAddedCells=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm|Paint") int32 DeathPaintTeethTouched=0;
    UFUNCTION(BlueprintPure,Category="Cataclysm|Paint") bool IsPaintWetAt(FVector Point) const;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") EMCCataclysmVariant Variant=EMCCataclysmVariant::Random;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") EMCCataclysmPhase Phase=EMCCataclysmPhase::Dormant;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") bool bCola=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") bool bFailed=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") double StartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") double PhaseStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") int32 DirtBatch=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") int32 JelliesLeft=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") int32 DirtLeft=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") int32 DirtTotal=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") TObjectPtr<AMCCoffeeFlood> Liquid;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") TArray<TObjectPtr<AMCCataclysmRaft>> Rafts;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Cataclysm") TArray<TObjectPtr<AMCCataclysmJelly>> Jellies;
    UPROPERTY(BlueprintReadOnly,Category="Cataclysm|Storm") TArray<TObjectPtr<AMCCataclysmDebris>> FloatingDebris;
    UPROPERTY(BlueprintReadOnly,Category="Cataclysm|Storm") int32 DebrisSpawned=0;
private:
    struct FCareBaseline { TWeakObjectPtr<UMCToothStatusComponent> Status; float Amount=0; };
    TArray<FCareBaseline> CareBaselines;
    TArray<FVector> RaftGroundAnchors;
    TMap<FIntPoint,TWeakObjectPtr<AMCMouthSurface>> PaintTiles;
    TSet<TWeakObjectPtr<AMCMouthSurface>> PendingPaintTiles;
    struct FDeathPaintSample { FVector UV=FVector::ZeroVector; float Baseline=0; };
    TMap<TWeakObjectPtr<AMCArenaTooth>,TArray<FDeathPaintSample>> DeathPaintToothSamples;
    FVector2D PaintGridOrigin=FVector2D::ZeroVector;
    float PaintTileWidth=1200.f;
    FIntPoint PaintGridSize=FIntPoint::ZeroValue;
    UPROPERTY() TObjectPtr<UMCDayPlan> DrinkPlan;
    UPROPERTY() TObjectPtr<AMCTongue> Tongue;
    FRandomStream Random;
    double NextObserveAt=0;
    double NextDebrisAt=0;
    void SetPhase(EMCCataclysmPhase Next);
    bool StartWave(bool bDeep);
    bool SpawnRafts();
    bool SpawnJellies();
    void UpdateFloatingDebris();
    void ClearFloatingDebris();
    void SeedDirt(int32 Count);
    bool InitializePaintGrid();
    bool DepositSupportedPaintSegment(FVector From,FVector To,float Radius,const TFunction<bool(FVector)>& CoverageAllowed={});
    void RegisterPaintStrokeInternal(FVector From,FVector To,float Radius,bool bVerifiedJellyPath);
    void FlushPaintTiles();
    void DepositDeathPaint(FVector Center,float Radius);
    void ObserveConsequences();
    void PublishManualStatus();
    void RemoveHazardActors();
    double Now() const;
};
