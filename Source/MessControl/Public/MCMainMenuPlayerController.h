#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MCMainMenuPlayerController.generated.h"

class UMCMainMenuWidget;

UCLASS()
class MESSCONTROL_API AMCMainMenuPlayerController : public APlayerController
{
    GENERATED_BODY()
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TObjectPtr<UMCMainMenuWidget> MenuWidget;
};
