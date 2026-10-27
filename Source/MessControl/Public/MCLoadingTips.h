#pragma once

#include "CoreMinimal.h"

class UDataTable;

/** Pure presentation timing, shared by the game-thread UMG and loading-thread Slate covers. */
struct FMCLoadingTipBlend
{
    int32 Current=INDEX_NONE;
    int32 Next=INDEX_NONE;
    float CurrentOpacity=0.f;
    float NextOpacity=0.f;
};

namespace MCLoadingTips
{
    inline constexpr double HoldSeconds=5.;
    inline constexpr double FadeSeconds=.2;
    MESSCONTROL_API int32 Count();
    MESSCONTROL_API FName Key(int32 Index);
    /** Resolve all rows on the game thread. The returned snapshot needs no UObject access. */
    MESSCONTROL_API TArray<FText> Resolve(UDataTable* Table,const FString& Culture);
    MESSCONTROL_API FText Heading(UDataTable* Table,const FString& Culture);
    MESSCONTROL_API FMCLoadingTipBlend Evaluate(double Seconds,int32 TipCount);
}
