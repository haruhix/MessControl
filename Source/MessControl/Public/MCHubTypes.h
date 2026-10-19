#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MCHubTypes.generated.h"

class APlayerState;
class UMCSingleDayProfile;
class UWorld;

UENUM(BlueprintType)
enum class EMCHubPhase : uint8
{
    Gathering,
    Selecting,
    Travelling
};

/** The server's current membership snapshot; entering a ready volume is not a client claim. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCHubPlayerReadiness
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Hub") TObjectPtr<APlayerState> Player = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Hub") bool bReady = false;
    UPROPERTY(BlueprintReadOnly, Category="Hub") bool bLoaded = false;

    bool operator==(const FMCHubPlayerReadiness& Other) const
    {
        return Player == Other.Player && bReady == Other.bReady && bLoaded == Other.bLoaded;
    }
};

/** A presentation day and an authored destination, not an arbitrary URL supplied by a client. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCHubDestinationRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub", meta=(ClampMin="1", ClampMax="7"))
    int32 Day = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") FName DisplayNameKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") FName DescriptionKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation", meta=(MultiLine=true)) FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Destination") bool bEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Destination") TSoftObjectPtr<UWorld> Map;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Destination") TSoftObjectPtr<UMCSingleDayProfile> SingleDayProfile;

    /** Syntax-only validation; the server additionally verifies that the map exists before travelling. */
    bool HasValidMapPath() const;
};
