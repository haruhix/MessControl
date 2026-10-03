#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCFoodCollisionEditorLibrary.generated.h"

class UDataTable;

/** Editor authoring and commandlet entry points. No decomposition runs in the game. */
UCLASS()
class MESSCONTROLEDITOR_API UMCFoodCollisionEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Mess Control|Food Collision")
    static bool BakeMenuCollision(UDataTable* Menu, bool bSave=true);

    UFUNCTION(BlueprintCallable, Category="Mess Control|Food Collision")
    static bool IsMenuCurrent(UDataTable* Menu);
};
