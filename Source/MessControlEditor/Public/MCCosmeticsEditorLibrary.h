#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCCosmeticsEditorLibrary.generated.h"
class UWidgetBlueprint;
class UMCCosmeticItemWidget;

UCLASS()
class MESSCONTROLEDITOR_API UMCCosmeticsEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Append Face/Hat controls and a scroll body, preserving the saved palette, skin slots, styles and graphs. */
    UFUNCTION(BlueprintCallable,Category="Mess Control|Cosmetics|Editor")
    static bool AddWardrobeCosmeticsPresentation(UWidgetBlueprint* Blueprint,TSubclassOf<UMCCosmeticItemWidget> TileClass);
    /** Upgrade only the known product-card presentation and its two grid hosts; preserve palette and panel shells. */
    UFUNCTION(BlueprintCallable,Category="Mess Control|Cosmetics|Editor")
    static bool RefreshHubProductCards(UWidgetBlueprint* Card,UWidgetBlueprint* Wardrobe,UWidgetBlueprint* Shop);
};
