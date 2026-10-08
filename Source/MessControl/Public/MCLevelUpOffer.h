#pragma once

#include "CoreMinimal.h"
#include "MCLevelUpOffer.generated.h"

/** IDs are server-generated and owner-only; clients submit an offer token and index. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCLevelUpOffer
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid OfferId;
    UPROPERTY(BlueprintReadOnly) int32 TeamLevel=0;
    UPROPERTY(BlueprintReadOnly) TArray<FName> PerkIDs;
    bool IsValid() const { return OfferId.IsValid() && TeamLevel>1 && PerkIDs.Num()==3; }
};
