#include "SharedMCNetworkFrames.h"
#include "MCGameState.h"
#include "Engine/World.h"

FGuid MCNetworkFrames::WorldLoadEpoch(const UWorld* World)
{
    const auto* State=World?World->GetGameState<AMCGameState>():nullptr;
    return State?State->GameplayLoadId:FGuid();
}
