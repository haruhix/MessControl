#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCVFXLabIceStation.generated.h"

class AMCTongue;
class AMCToothCharacter;
class AMCIceEvent;
class AMCIceBlock;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EMCVFXLabIceKind : uint8
{
    FreezeThawAndRescue,
    NovaProtection,
    CrystalAndIcicles,
    CentralCrystal,
    IceBlocks,
    PhysicsReaction
};

/** Repeatable lab demonstrations driven by the real authoritative gameplay actors. */
UCLASS()
class MESSCONTROL_API AMCVFXLabIceStation : public AActor
{
    GENERATED_BODY()
public:
    AMCVFXLabIceStation();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_Status,BlueprintReadWrite,Category="VFX Lab") EMCVFXLabIceKind Kind=EMCVFXLabIceKind::FreezeThawAndRescue;
    UPROPERTY(EditAnywhere,Replicated,BlueprintReadWrite,Category="VFX Lab") TObjectPtr<AMCTongue> Floor;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab") bool bAutoRun=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VFX Lab",meta=(ClampMin="24",Units="s")) float CycleSeconds=28;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="VFX Lab") bool bRunning=false;
    /** Counts actual successful/failed observations, not merely requested cues. */
    UPROPERTY(ReplicatedUsing=OnRep_Status,BlueprintReadOnly,Category="VFX Lab") int32 Passed=0;
    UPROPERTY(ReplicatedUsing=OnRep_Status,BlueprintReadOnly,Category="VFX Lab") int32 Failed=0;
    UPROPERTY(ReplicatedUsing=OnRep_Status,BlueprintReadOnly,Category="VFX Lab") int32 Cycles=0;
    UPROPERTY(ReplicatedUsing=OnRep_Status,BlueprintReadOnly,Category="VFX Lab") int32 PassedCycles=0;
    UPROPERTY(ReplicatedUsing=OnRep_Status,BlueprintReadOnly,Category="VFX Lab") int32 FailedCycles=0;
    UPROPERTY(ReplicatedUsing=OnRep_Status,BlueprintReadOnly,Category="VFX Lab") FString Status;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VFX Lab") TObjectPtr<UTextRenderComponent> StatusLabel;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="VFX Lab") void Reset();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="VFX Lab") void SetRunning(bool Running);

private:
    void BeginCycle();
    void Cleanup();
    void Advance(double Time);
    void Observe(bool Condition,const TCHAR* Result);
    void RecordCycle();
    void Show(const FString& Message);
    bool SurfaceAt(FVector Local,FHitResult& Hit) const;
    bool PlaceSubject(int32 Index,FVector Local,FVector Facing=FVector::ForwardVector);
    AMCToothCharacter* SpawnSubject(FVector Local,int32 Index);
    bool StartWinter();
    bool WinterVisualsReady() const;
    void HitCue(AMCToothCharacter* Worker);
    UFUNCTION() void OnRep_Status();
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> OwnedActors;
    UPROPERTY(Transient) TArray<TObjectPtr<AMCToothCharacter>> Subjects;
    UPROPERTY(Transient) TArray<TObjectPtr<AMCIceBlock>> Blocks;
    UPROPERTY(Transient) TObjectPtr<AMCIceEvent> Winter;
    double CycleStartedAt=0;
    double NextActionAt=0;
    double ActionDeadline=0;
    int32 Step=0;
    bool bComplete=false;
    bool bCycleFailed=false;
    bool bCycleRecorded=false;
    float Baseline=0;
    float PeakFreeze=0;
    int32 BaselineKnockdowns=0;
    FVector BaselinePosition=FVector::ZeroVector;
    FString LastFailure;
};
