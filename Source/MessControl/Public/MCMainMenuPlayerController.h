#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MCMainMenuPlayerController.generated.h"

class UMCMainMenuWidget;
class UMCSoundPalette;
class UMCSceneAudioComponent;

UCLASS()
class MESSCONTROL_API AMCMainMenuPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio") TObjectPtr<UMCSoundPalette> BackgroundSoundPalette;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Audio") TObjectPtr<UMCSceneAudioComponent> SceneAudio;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TObjectPtr<UMCMainMenuWidget> MenuWidget;
};
