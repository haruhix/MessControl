#include "MCGameState.h"
#include "Net/UnrealNetwork.h"
float AMCGameState::SecondsLeft() const { return FMath::Max(0., PhaseEndsAt - GetServerWorldTimeSeconds()); }
void AMCGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCGameState, Day); DOREPLIFETIME(AMCGameState, Phase);
    DOREPLIFETIME(AMCGameState, MouthHealth); DOREPLIFETIME(AMCGameState, TasksLeft);
    DOREPLIFETIME(AMCGameState, TasksTotal); DOREPLIFETIME(AMCGameState, PhaseEndsAt);
    DOREPLIFETIME(AMCGameState, CurrentEvent); DOREPLIFETIME(AMCGameState, RunSeed);
}
