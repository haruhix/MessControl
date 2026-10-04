#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCRoguelikeEditorLibrary.generated.h"

class UWorld;

/** Saved level authoring only; geometry and navigation generation never run in a packaged match. */
UCLASS()
class MESSCONTROLEDITOR_API UMCRoguelikeEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Extent is the box half-size in centimetres. Reuses only the volume owned by this helper. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Roguelike|Navigation")
    static bool ConfigureBossNavigation(UWorld* World,FVector Center,FVector Extent);
};
