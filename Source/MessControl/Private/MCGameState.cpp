#include "MCGameState.h"
#include "MCProgressionComponent.h"
#include "MCSingleDayDirector.h"
#include "MCArenaTooth.h"
#include "Net/UnrealNetwork.h"
AMCGameState::AMCGameState() { Progression=CreateDefaultSubobject<UMCProgressionComponent>(TEXT("Progression")); }
int32 AMCGameState::AvailableArenaTeeth() const
{
    int32 Count=0; for (const AMCArenaTooth* Tooth:ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable()) ++Count;
    return Count;
}
float AMCGameState::SecondsLeft() const { return FMath::Max(0., PhaseEndsAt - GetServerWorldTimeSeconds()); }
void AMCGameState::RecordDirectorDecision(const FString& Decision)
{
    if (!HasAuthority() || Decision.IsEmpty()) return;
    DirectorDecisionLog.Add(Decision);
    if (DirectorDecisionLog.Num()>128) DirectorDecisionLog.RemoveAt(0,DirectorDecisionLog.Num()-128);
    DirectorState.DecisionLog=DirectorDecisionLog;
    DirectorState.LastDecision=DirectorDecisionLog.Last();
    ForceNetUpdate();
}
void AMCGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCGameState,bSingleDayLoop);
    DOREPLIFETIME(AMCGameState,SingleDayDirector);
    DOREPLIFETIME(AMCGameState,TeamToolUpgrades);
    DOREPLIFETIME(AMCGameState,DirectorState);
    DOREPLIFETIME(AMCGameState,DirectorDecisionLog);
    DOREPLIFETIME(AMCGameState,bLobbyWaiting);
    DOREPLIFETIME(AMCGameState,bTutorialActive);
    DOREPLIFETIME(AMCGameState,LobbyLoadedPlayers);
    DOREPLIFETIME(AMCGameState, RunSettings);
    DOREPLIFETIME(AMCGameState,StepStartedAt); DOREPLIFETIME(AMCGameState,PreviousStepFailed);
    DOREPLIFETIME(AMCGameState, bDevManualEvents);
    DOREPLIFETIME(AMCGameState, DayPlan); DOREPLIFETIME(AMCGameState, StepIndex); DOREPLIFETIME(AMCGameState, bPhysicalBrushes);
    DOREPLIFETIME(AMCGameState, bDayOneComplete); DOREPLIFETIME(AMCGameState, DayStartedAt); DOREPLIFETIME(AMCGameState, FailedEvents);
    DOREPLIFETIME(AMCGameState, ArenaTeeth);
    DOREPLIFETIME(AMCGameState, Day); DOREPLIFETIME(AMCGameState, Phase);
    DOREPLIFETIME(AMCGameState, MouthHealth); DOREPLIFETIME(AMCGameState, TasksLeft);
    DOREPLIFETIME(AMCGameState, TasksTotal); DOREPLIFETIME(AMCGameState, PhaseEndsAt);
    DOREPLIFETIME(AMCGameState, CurrentEvent); DOREPLIFETIME(AMCGameState, RunSeed);
}
