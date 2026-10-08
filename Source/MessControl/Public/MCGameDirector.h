#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCGameDirectorTypes.h"
#include "MCGameDirectorProfile.h"
#include "MCGameDirector.generated.h"
class UMCDayPlan;
class AMCDayDirector;
class AMCGameState;
class AMCRoguelikeDirector;

/** One server scheduler owns all exogenous events; committed consequences remain real. */
UCLASS()
class MESSCONTROL_API AMCGameDirector : public AActor
{
    GENERATED_BODY()
public:
    AMCGameDirector();
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    static AMCGameDirector* Find(UWorld* World);
    void InitializeRun(UMCDayPlan* Mechanics,UMCGameDirectorProfile* Profile);
    void BeginDay(int32 Day);
    /** A bounded interval between authored events; its timeout never ends the run. */
    void BeginInterlude(float Seconds);
    /** Ordinary support between key events. Zero seconds means no deadline or automatic end. */
    void BeginSupport(float Seconds=0.f);
    void Stop();
    bool IsManagingEvents() const;
    bool IsLaunchingEvent(EMCGameDirectorEvent Kind) const;
    bool CanStartSwallow() const;
    bool CanStartRewardInteraction() const;
    bool TryDispatchReward(AMCRoguelikeDirector* Rewards);
    bool HasOutstandingWork() const;
    int32 PendingFoodCount() const;
    /** Observes the actual world. Also used by focused automation diagnostics. */
    FMCGameDirectorState Observe() const;
    TArray<FMCGameDirectorCandidate> EvaluateCandidates(const FMCGameDirectorState& Seen,double Now) const;
    static FString EventName(EMCGameDirectorEvent Kind);
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Director") TObjectPtr<UMCGameDirectorProfile> Settings;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Director") TObjectPtr<UMCDayPlan> Mechanics;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Director") FMCGameDirectorDaySettings DaySettings;
private:
    struct FTicket {
        int32 Id=0,Day=0,Batch=0,Patches=0,ToothCount=0,IceCount=1;
        EMCGameDirectorEvent Kind=EMCGameDirectorEvent::Food;
        FName FoodRow;
        double RequestedAt=0,WarningAt=-1,StartedAt=-1;
        bool bStarted=false,bDone=false;
        TWeakObjectPtr<AActor> Actor;
    };
    TArray<FTicket> Tickets;
    UPROPERTY() TObjectPtr<AMCDayDirector> Services;
    bool bManaging=false;
    bool bInterlude=false;
    bool bSupportMode=false;
    TOptional<EMCGameDirectorEvent> LaunchGrant;
    int32 NextId=0,PlannedFoodTotal=0,SpawnedFoodTotal=0,FinishedFoodTotal=0;
    double DayStartedAt=0,DayEndsAt=0,LastSpecialAt=-100,NextFoodAt=0,PhaseStartedAt=0,RestUntil=0,NextObserveAt=0;
    EMCGameDirectorPacing Pacing=EMCGameDirectorPacing::Intermission;
    FRandomStream Random;
    FString LastReason,CurrentTitle;
    FString CandidateTitle;
    float Difficulty=.7f,Throughput=1,TargetPressure=.4f,PreviousWork=0,AddedWork=0;
    double NextDecisionAt=0,SampleAt=0;
    TMap<EMCGameDirectorEvent,double> LastEventAt;
    TMap<EMCGameDirectorEvent,int32> DayEventCounts;
    TArray<EMCGameDirectorEvent> RecentKinds;
    TArray<FMCGameDirectorCandidate> CandidateScores;
    float WaitWeight=0,WaitProbability=0;
    TArray<FString> Log;
    FName ChooseFoodRow(EMCGameDirectorEvent Kind,const FMCGameDirectorState& Seen) const;
    FTicket MakeTicket(EMCGameDirectorEvent Kind,const FMCGameDirectorState& Seen) const;
    void UpdateAdaptation(const FMCGameDirectorState& Seen,double Now,float Dt);
    float ComputeWaitWeight(const FMCGameDirectorState& Seen) const;
    bool SelectEvent(const FMCGameDirectorState& Seen,double Now);
    void CancelReservation(const FString& Reason,double Now);
    bool TryStart(FTicket& Ticket,const FMCGameDirectorState& Seen,double Now);
    float ForecastPressure(const FTicket& Ticket,const FMCGameDirectorState& Seen) const;
    void ResolveTickets(double Now);
    void Publish(const FMCGameDirectorState& Seen,const FString& Reason,double Now);
    void Record(const FString& Decision);
    void FinishDay(const FMCGameDirectorState& Seen,double Now);
};
