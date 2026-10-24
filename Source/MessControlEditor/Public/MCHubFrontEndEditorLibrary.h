#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCHubFrontEndEditorLibrary.generated.h"

class UWidgetBlueprint;

/** Narrow, explicit migrations for the approved Play -> Hub flow. Never saves unrelated packages. */
UCLASS()
class MESSCONTROLEDITOR_API UMCHubFrontEndEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Rewire only the real PlayButton/OnClicked source event. Leaves every day selector graph intact. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Hub|Editor")
    static bool RoutePlayButtonToHub(UWidgetBlueprint* Blueprint);

    /** Append an editable pending/failure status at the 3D menu root, without replacing its layout. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Hub|Editor")
    static bool AddPlayStatusPresentation(UWidgetBlueprint* Blueprint);

    /** Append missing Hub guidance or Pause invitation branches; retain the existing tree and materials. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Hub|Editor")
    static bool AddHubGuidancePresentation(UWidgetBlueprint* Blueprint);

    /** Persist only the missing voice header/mute-column branches in the existing authored Scoreboard. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Hub|Editor")
    static bool AddScoreboardVoicePresentation(UWidgetBlueprint* Blueprint);

    /** Persist only missing Hub party invitation/privacy controls in the existing Scoreboard tree. */
    UFUNCTION(BlueprintCallable, Category="Mess Control|Hub|Editor")
    static bool AddScoreboardHubPartyPresentation(UWidgetBlueprint* Blueprint);
};
