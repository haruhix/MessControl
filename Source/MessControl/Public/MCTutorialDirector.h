#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCTutorialTypes.h"
#include "MCToothStatusComponent.h"
#include "MCToothCalculusComponent.h"
#include "MCTutorialDirector.generated.h"

class AMCToothCharacter;
class APlayerState;
class AMCPlayerState;
class AMCArenaTooth;
class AMCMouthSurface;
class AMCFoodActor;
class AMCCoffeeFlood;
class AMCToothpick;
class UMCDayPlan;
class UNiagaraComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
class UTextRenderComponent;

/** A separate server-owned lesson runner. It never advances or rewards the ordinary day director. */
UCLASS()
class MESSCONTROL_API AMCTutorialDirector : public AActor
{
    GENERATED_BODY()
public:
    AMCTutorialDirector();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void Start(UMCDayPlan* Plan=nullptr,bool bUseShortFlow=false);
    void Stop();
    void SetLoaded(AMCPlayerState* Player);
    void SetReady(AMCPlayerState* Player,bool bReady);
    void NotifyAction(AMCToothCharacter* Worker,EMCTutorialAction Action,AActor* Target=nullptr);
    void NotifyIncorrectSort(AMCToothCharacter* Worker,AActor* Target);
    static AMCTutorialDirector* Find(UWorld* World);
    static bool IsSafeTutorial(UWorld* World);
    static bool IsTutorialTarget(const AActor* Actor);
    static constexpr int32 TutorialFoodBatch=-10000;

    const FMCTutorialPlayerProgress* GetPlayerProgress(const APlayerState* Player) const;
    int32 GetCompletedPlayers() const;
    int32 GetRequiredPlayers() const { return Players.Num(); }
    bool IsComplete() const { return Stage==EMCTutorialStage::Complete; }
    bool UsesShortFlow() const { return bShortFlow; }
    FSimpleMulticastDelegate OnTutorialFinished;

    UPROPERTY(ReplicatedUsing=OnRep_Presentation,BlueprintReadOnly,Category="Tutorial") EMCTutorialStage Stage=EMCTutorialStage::Loading;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") FText Title;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") FText FairyLine;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") FText Instruction;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") double StageStartedAt=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") double StageEndsAt=0;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation,BlueprintReadOnly,Category="Tutorial") TArray<FMCTutorialPlayerProgress> Players;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") int32 TeamTasksLeft=0;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Tutorial") int32 TeamTasksTotal=0;

private:
    struct FToothSnapshot
    {
        TWeakObjectPtr<AMCArenaTooth> Tooth;
        FMCToothStatus Status;
        FMCCalculusState Calculus;
        TArray<uint8> GrimeMask;
    };
    UFUNCTION() void OnRep_Presentation();
    void EnterStage(EMCTutorialStage NewStage);
    void SyncPlayers();
    void EnsureLessonTargets();
    void ClearLessonTargets();
    void RestoreArena();
    void UpdatePresentation();
    void RescuePlayers();
    AMCArenaTooth* AssignTooth(const FMCTutorialPlayerProgress& Player);
    AMCMouthSurface* SpawnTonguePatch(const FMCTutorialPlayerProgress& Player);
    AMCFoodActor* SpawnFood(FName RowName,FVector Position,AMCPlayerState* OwnerPlayer,bool bFragment=false,bool bSpoiled=false);
    FVector LessonPosition(const FMCTutorialPlayerProgress& Player,int32 Index) const;
    bool TargetIsOwn(const FMCTutorialPlayerProgress& Player,const AActor* Target) const;
    double Now() const;
    int32 CountTutorialFood() const;
    void EnsureSharedToothpick();
    UPROPERTY(Transient) TObjectPtr<AMCToothpick> LessonToothpick;
    UPROPERTY(Transient) TObjectPtr<AMCMouthSurface> LessonUlcer;

    UPROPERTY(Transient) TObjectPtr<UMCDayPlan> Settings;
    UPROPERTY(Transient) TObjectPtr<AMCCoffeeFlood> Flood;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> SpawnedActors;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> PartyFoam;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> PartyFoamSystem;
    UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> GoalLabel;
    TArray<FToothSnapshot> ToothSnapshots;
    TMap<TWeakObjectPtr<AMCPlayerState>,FMCToothStatus> PlayerSnapshots;
    TMap<TWeakObjectPtr<AMCPlayerState>,FVector> SafePositions;
    TWeakObjectPtr<UPrimitiveComponent> Highlighted;
    bool bPreviousCustomDepth=false;
    int32 PreviousStencil=0;
    FRandomStream Random;
    bool bStarted=false,bFinishedBroadcast=false,bRestored=false;
    UPROPERTY(Replicated) bool bShortFlow=false;
    int32 BreakfastSpawned=0;
    float MaintenanceClock=0;
    FText DefaultFairyLine;
    double FeedbackEndsAt=0;
};
