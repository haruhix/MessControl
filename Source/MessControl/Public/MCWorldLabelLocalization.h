#pragma once
#include "CoreMinimal.h"
class UDataTable;
class UTextRenderComponent;

/** Exact owner keys; a source-local presenter supplies only already resident UI overrides. */
namespace MCWorldLabelLocalization
{
    /** Original owner assignment, recording the post-SetText native copy for safe later refresh. */
    MESSCONTROL_API void Apply(UTextRenderComponent* SourceLabel,const FText& ActualCopy);
    MESSCONTROL_API UDataTable* ResidentTable(const UTextRenderComponent* SourceLabel);
    MESSCONTROL_API FText Resolve(const UTextRenderComponent* SourceLabel,FName Key,const FText& Fallback);
    MESSCONTROL_API FText Format(const UTextRenderComponent* SourceLabel,FName Key,const FText& Fallback,const FFormatNamedArguments& ActualArguments);
}
