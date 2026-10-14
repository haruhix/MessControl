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
    void Stop(bool bClearEventDirt=false);
    void RegisterDirtyPatch(FVector Center, float Radius, bool bDeathBurst);
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
    void ObserveConsequences();
    void PublishManualStatus();
    void RemoveHazardActors();
    double Now() const;
};
