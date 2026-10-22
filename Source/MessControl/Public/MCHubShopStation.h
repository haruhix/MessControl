#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCHubShopStation.generated.h"

class APawn;
class APlayerController;
class UBoxComponent;
class UWidgetComponent;

/** Interaction bounds for the existing vending machine. No catalog or purchase logic. */
UCLASS(Blueprintable)
class MESSCONTROL_API AMCHubShopStation : public AActor
{
    GENERATED_BODY()
public:
    AMCHubShopStation();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop") TObjectPtr<UBoxComponent> InteractionVolume;
    /** Editable anchor for the keycap above the existing vending machine. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop|Prompt") TObjectPtr<UWidgetComponent> InteractionPrompt;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") bool bEnabled=true;
    UFUNCTION(BlueprintPure, Category="Shop") bool ContainsPawn(const APawn* Pawn) const;
    /** Owning-client presentation only; this does not replicate or enable interaction. */
    UFUNCTION(BlueprintCallable, Category="Shop|Prompt") void SetLocalPromptVisible(APlayerController* LocalController,bool bVisible);
protected:
    virtual void BeginPlay() override;
};
