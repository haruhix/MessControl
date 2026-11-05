#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCClothingCommerceValidationLibrary.generated.h"

/** Isolated native commerce contract check. Never uses a live player or writes source assets/profiles. */
UCLASS()
class MESSCONTROLEDITOR_API UMCClothingCommerceValidationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Idle Editor only; the two fixed saved canonical tables must already be loaded and clean.
     * Creates transient cloned tables and an unplayed private authority world, then destroys it.
     * Returns JSON, not a gameplay/PIE/visual-acceptance claim. No output files are written here. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Clothing|Validation")
    static FString ValidateTransientCommerce();
};
