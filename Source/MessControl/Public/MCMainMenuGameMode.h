#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MCMainMenuGameMode.generated.h"

/** Separate mode keeps menu startup from constructing or advancing the gameplay director. */
UCLASS()
class MESSCONTROL_API AMCMainMenuGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMCMainMenuGameMode();
};
