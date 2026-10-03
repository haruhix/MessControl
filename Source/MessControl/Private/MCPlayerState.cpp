#include "MCPlayerState.h"
#include "Net/UnrealNetwork.h"

int32 FMCScoreRewards::ForTask(EMCScoreTask Task) const
{
    switch (Task)
    {
    case EMCScoreTask::Coffee: return FMath::Clamp(Coffee,0,100000);
    case EMCScoreTask::Repair: return FMath::Clamp(Repair,0,100000);
    case EMCScoreTask::Food: return FMath::Clamp(Food,0,100000);
    case EMCScoreTask::Ulcer: return FMath::Clamp(Ulcer,0,100000);
    case EMCScoreTask::Ice: return FMath::Clamp(Ice,0,100000);
    }
    return 0;
}
void AMCPlayerState::AddPoints(int32 Amount)
{
    if (!HasAuthority() || Amount<=0) return;
    Points=static_cast<int32>(FMath::Min<int64>(int64(Points)+Amount,MAX_int32));
    SetScore(float(Points)); ForceNetUpdate();
}
void AMCPlayerState::ResetMatchScore()
{
    if (!HasAuthority()) return;
    Points=0; SetScore(0); Alarm=EMCPlayerAlarm::None; AlarmUntil=0; ForceNetUpdate();
}
void AMCPlayerState::CopyProperties(APlayerState* Target)
{
    Super::CopyProperties(Target);
    if (auto* State=Cast<AMCPlayerState>(Target))
    { State->Points=Points; State->PlayerColor=PlayerColor; State->bSessionHost=bSessionHost; }
}
void AMCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCPlayerState,Points); DOREPLIFETIME(AMCPlayerState,bSessionHost);
    DOREPLIFETIME(AMCPlayerState,PlayerColor); DOREPLIFETIME(AMCPlayerState,Alarm); DOREPLIFETIME(AMCPlayerState,AlarmUntil);
}
