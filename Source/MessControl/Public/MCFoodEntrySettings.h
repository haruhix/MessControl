#pragma once
#include "CoreMinimal.h"
#include "MCFoodEntrySettings.generated.h"

/** Readable, single-piece arrivals through the front opening of the mouth. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCFoodEntrySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.75",ClampMax="3",Units="s")) float FlightSeconds=1.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="150",ClampMax="900",Units="cm")) float EntryHeight=250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="600",Units="cm")) float OutsideDistance=250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0",ClampMax="180")) float PushSpeed=140.f;

    void Sanitize();
    bool BuildTrajectory(const FBox& TongueBounds,FVector LandingCenter,float GravityZ,FVector& Start,FVector& Velocity) const;
};
