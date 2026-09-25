#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "Net/UnrealNetwork.h"
int32 AMCGameState::AvailableArenaTeeth() const
{
    int32 Count=0; for (const AMCArenaTooth* Tooth:ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable()) ++Count;
    return Count;
}
float AMCGameState::SecondsLeft() const { return FMath::Max(0., PhaseEndsAt - GetServerWorldTimeSeconds()); }
void AMCGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCGameState, RunSettings);
    DOREPLIFETIME(AMCGameState, bDevManualEvents);
    DOREPLIFETIME(AMCGameState, DayPlan); DOREPLIFETIME(AMCGameState, StepIndex); DOREPLIFETIME(AMCGameState, bPhysicalBrushes);
    DOREPLIFETIME(AMCGameState, bDayOneComplete); DOREPLIFETIME(AMCGameState, DayStartedAt); DOREPLIFETIME(AMCGameState, FailedEvents);
    DOREPLIFETIME(AMCGameState, ArenaTeeth);
    DOREPLIFETIME(AMCGameState, Day); DOREPLIFETIME(AMCGameState, Phase);
    DOREPLIFETIME(AMCGameState, MouthHealth); DOREPLIFETIME(AMCGameState, TasksLeft);
    DOREPLIFETIME(AMCGameState, TasksTotal); DOREPLIFETIME(AMCGameState, PhaseEndsAt);
    DOREPLIFETIME(AMCGameState, CurrentEvent); DOREPLIFETIME(AMCGameState, RunSeed);
}
