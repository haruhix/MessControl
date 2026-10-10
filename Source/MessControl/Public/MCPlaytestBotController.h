#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "MCInventoryComponent.h"
#include "MCPlaytestBotTypes.h"
#include "MCPlaytestBotController.generated.h"

class AMCToothCharacter;
class AMCNutBoss;

UENUM()
enum class EMCPlaytestBotGoal : uint8 { Idle, Clean, Repair, Spray, BreakFood, CollectFood, PullFood, Deliver, Calculus, Fight, Escape, Recover };

/** Server-side playtester. All work is performed through the ordinary pawn input actions. */
UCLASS(Transient)
class MESSCONTROL_API AMCPlaytestBotController : public AAIController
{
    GENERATED_BODY()
public:
    AMCPlaytestBotController();
    void Configure(EMCPlaytestBotSkill Skill,int32 BotIndex,int32 Seed);
    virtual void Tick(float DeltaSeconds) override;
    FString GetActivity() const;
    int32 GetPathFailureCount() const { return PathFailures; }
    float GetIdleSeconds() const { return IdleSeconds; }
    float GetWorkSeconds() const { return WorkSeconds; }
    AActor* GetTaskTarget() const { return Target.Get(); }
    EMCPlaytestBotSkill GetSkill() const { return Skill; }
    bool IsApproachPositionClear(FVector PawnCenter) const;
    /** Isolated lab ownership: retain the ordinary approach/input code, without global task selection. */
    void SetLabControlled(bool bEnabled);
    void DriveLabTask(AActor* Actor,EMCPlaytestBotGoal Task);
protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnMoveCompleted(FAIRequestID RequestID,const FPathFollowingResult& Result) override;
private:
    void Decide();
    void ChooseTask();
    bool IsTaskValid() const;
    bool CanObserve(AActor* Actor) const;
    FVector TaskPoint(AActor* Actor) const;
    void ReleaseInputs(bool bDropCollection=false,bool bKeepSprint=false);
    void MoveTowards(FVector Destination,float Acceptance=30.f);
    void WorkAtTarget();
    void FailPath();
    bool PlanCleanApproach(class AMCArenaTooth* Tooth);
    bool PlanLabFloorApproach(class AMCMouthSurface* Surface);
    bool ProjectStandingPosition(FVector Candidate,FVector& PawnCenter) const;
    void ResetApproach(bool bRememberFailure=false);
    void RecoverFromStall();
    bool ReactToHazard();
    bool InterruptForNutThreat();
    bool IsNutEncounterActive() const;
    bool TryNutFlank(AMCNutBoss* Boss);
    double TargetProgress() const;
    UPROPERTY(Transient) TObjectPtr<AMCToothCharacter> Hero;
    TWeakObjectPtr<AActor> Target;
    EMCPlaytestBotGoal Goal=EMCPlaytestBotGoal::Idle;
    EMCPlaytestBotSkill Skill=EMCPlaytestBotSkill::Regular;
    FMCPlaytestBotTuning Tuning;
    FRandomStream Random;
    FTimerHandle DecisionTimer;
    TMap<TWeakObjectPtr<AActor>,double> FailedTargets;
    FVector LastRequestedGoal=FVector::ZeroVector;
    FVector LastProgressPosition=FVector::ZeroVector;
    FVector EscapeDestination=FVector::ZeroVector;
    FVector WorkStand=FVector::ZeroVector,WorkAim=FVector::ZeroVector;
    TArray<TPair<FVector,double>> FailedApproaches;
    bool bHasWorkApproach=false;
    double RecoveryUntil=0,JumpReleaseAt=0;
    double NextTaskAt=0,NextMoveAt=0,NextJumpAt=0,NextHesitationAt=0,NextExploreAt=0;
    FVector ExploreDestination=FVector::ZeroVector;
    double HazardSeenAt=-1,EscapeUntil=0,ProgressCheckedAt=0;
    double LastWorkProgressAt=0,LastTargetProgress=0;
    float AimError=0,IdleSeconds=0,WorkSeconds=0;
    int32 PathFailures=0;
    bool bConfigured=false;
    bool bLabControlled=false;
    bool bSuspended=false;
    bool bNutBossCare=false;
    int32 NutFlankSide=1;
    TWeakObjectPtr<AMCNutBoss> NutFlankTarget;
    TArray<FVector> NutFlankPoints;
    FVector NutFlankCenter=FVector::ZeroVector;
    int32 NutFlankPointIndex=0;
    double NutFlankUntil=0,NextNutFlankAt=0;
};
