#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCRunRules.generated.h"

/** Values copied by the server at the start of a run and shared with all clients. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCRunSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Run", meta=(ClampMin="1"))
    float MaxMouthHealth = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Run", meta=(ClampMin="1"))
    int32 DaysToSurvive = 7;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cooperation", meta=(ClampMin="1", ClampMax="4"))
    int32 MaxPlayers = 4;

    // Number of concrete teeth spawned at run start, not a live respawn counter.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arena", meta=(ClampMin="0", ToolTip="Arena actors spawned at run start, excluding starting player characters. Respawn consumption is a later step."))
    int32 InitialArenaTeeth = 8;

    void Sanitize();
};

/** Designer-owned configuration. Runtime health and remaining teeth belong to the game state. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCRunRules : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run Rules")
    FMCRunSettings Settings;
};
