#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCVFXLabToolStation.generated.h"

class AMCTongue;
class AMCToothCharacter;
class AMCPlaytestBotController;
class AMCArenaTooth;
class AMCMouthSurface;
class AMCFoodActor;
class AMCIceBlock;
class AMCCoffeeFlood;
class AMCFirePatch;
class UTextRenderComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMCDayPlan;

UENUM(BlueprintType)
enum class EMCVFXLabToolCase : uint8
{
    FloorBrush, FloorMeshaBrush, ToothBrush, ToothMeshaBrush,
    SelfBrush, SelfRepair, ToothRepair, SprayUlcer, SprayFire,
    WatergunCare, WatergunPressure, PickaxeCalculus, BufferCalculus,
    PickaxeIce, FrozenLegs, BufferAoE, BufferWall,
    KnifeFood, ChainsawFood, ChainsawWall, CoffeeWave
};

/** Repeating real pawn inputs and ordinary gameplay fixtures; no cosmetic-only damage or cleaning. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCVFXLabToolStation : public AActor
{
    GENERATED_BODY()
public:
    AMCVFXLabToolStation();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Lab") EMCVFXLabToolCase StationKind=EMCVFXLabToolCase::FloorBrush;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Lab") TObjectPtr<AMCTongue> Floor;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Lab") bool bAutoRun=true;
    /** Maximum time allowed for an observed completion, including approach and animation. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Lab",meta=(ClampMin="8",ClampMax="180")) float CycleSeconds=45;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Lab",meta=(ClampMin="0.5",ClampMax="10")) float RefillPause=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Lab") TSoftObjectPtr<UStaticMesh> ToothMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Lab") TSoftClassPtr<AMCToothCharacter> WorkerClass;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") bool bRunning=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") bool Passed=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") bool Failed=false;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") int32 Cycles=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") int32 PassedCycles=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") int32 FailedCycles=0;
    UPROPERTY(ReplicatedUsing=OnRep_Status,BlueprintReadOnly,Category="Lab") FString Status;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") float Progress=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Lab") TObjectPtr<AMCToothCharacter> Worker;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Lab") void Reset();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Lab") void SetRunning(bool bEnabled);
private:
    UFUNCTION() void OnRep_Status();
    bool SpawnWorker();
    bool PrepareCycle();
    void DriveInputs(float Dt);
    void FinishCycle(bool bSuccess,const FString& Reason);
    void ClearFixtures();
    void ClearAll();
    void SetStatus(const FString& Text);
    FVector GroundPoint(FVector LocalOffset) const;
    AMCMouthSurface* SpawnPatch(bool bUlcer,FVector LocalOffset,float HalfSize=100);
    AMCIceBlock* SpawnIce(FVector WorldPoint,float Size=65,float Health=150);
    AMCFoodActor* SpawnFood(FVector WorldPoint,bool bHard,float Health=75);
    AMCArenaTooth* SpawnTooth();
    void SpawnWall(FVector Point,FVector HalfExtent,FRotator Rotation);
    bool GrantUpgrade(uint8 Kind);
    float Remaining() const;
    void HoldTool(uint8 Slot,bool bSelf=false);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Readout;
    UPROPERTY(Transient) TObjectPtr<AMCPlaytestBotController> Bot;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Fixtures;
    UPROPERTY(Transient) TObjectPtr<AMCArenaTooth> Tooth;
    UPROPERTY(Transient) TObjectPtr<AMCMouthSurface> Patch;
    UPROPERTY(Transient) TObjectPtr<AMCFoodActor> Food;
    UPROPERTY(Transient) TArray<TObjectPtr<AMCIceBlock>> Ice;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Wall;
    UPROPERTY(Transient) TObjectPtr<AMCFirePatch> Fire;
    UPROPERTY(Transient) TObjectPtr<AMCCoffeeFlood> Flood;
    UPROPERTY(Transient) TObjectPtr<UMCDayPlan> CoffeePlan;
    double StartedAt=0,NextCycleAt=0,NextReadoutAt=0;
    float InitialRemaining=1;
    int32 InitialContacts=0,InitialHits=0,ResidueBatch=0;
    bool bCycleActive=false,bWallOpened=false,bSawWetFloor=false,bSawCoffeeOnWorker=false;
    bool bPressureSelected=false,bBufferPlaced=false;
    bool bSawMultiHit=false,bHadCalculus=false;
    TArray<float> LastIceHealth;
};
