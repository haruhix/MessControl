#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCHubWardrobeStation.generated.h"

class APawn;
class APlayerController;
class UBoxComponent;
class UWidgetComponent;

/** Editable interaction bounds for an existing hub booth; creates no new scenery. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCHubWardrobeStation : public AActor
{
    GENERATED_BODY()
public:
    AMCHubWardrobeStation();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wardrobe")
    TObjectPtr<UBoxComponent> InteractionVolume;

    /** Move this component in the editor to place the keycap above the authored booth. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wardrobe|Prompt")
    TObjectPtr<UWidgetComponent> InteractionPrompt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wardrobe")
    bool bEnabled=true;

    UFUNCTION(BlueprintPure, Category="Wardrobe")
    bool ContainsPawn(const APawn* Pawn) const;

    /** Local presentation only. Called by the owning player's existing proximity scan, never an RPC. */
    UFUNCTION(BlueprintCallable, Category="Wardrobe|Prompt")
    void SetLocalPromptVisible(APlayerController* LocalController,bool bVisible);

protected:
    virtual void BeginPlay() override;
};
