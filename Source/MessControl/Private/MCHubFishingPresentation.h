#pragma once

#include "CoreMinimal.h"

struct FGeometry;
class FSlateWindowElementList;
class UDataTable;
class UMCFishingRodComponent;

namespace MCHubFishingPresentation
{
    enum class EHint : uint8 { Cast, CastGamepad, Wait, Hook, HookGamepad, Reel, Count };

    FText ResolveHint(UDataTable* Table,EHint Hint,const FString& Culture=FString());
    void AppendHints(TArray<FText>& Copies,UDataTable* Table,const FString& Culture=FString());
    int32 DrawMeter(const UMCFishingRodComponent& Rod,bool bGamepad,const FGeometry& Geometry,
        FSlateWindowElementList& Elements,int32 Layer,const FLinearColor& Tint);
}
