#include "MCRunRules.h"

void FMCRunSettings::Sanitize()
{
    MaxMouthHealth = FMath::IsFinite(MaxMouthHealth) ? FMath::Max(1.f, MaxMouthHealth) : 100.f;
    DaysToSurvive = FMath::Max(1, DaysToSurvive);
    MaxPlayers = FMath::Clamp(MaxPlayers, 1, 4);
    InitialArenaTeeth = FMath::Max(0, InitialArenaTeeth);
}
