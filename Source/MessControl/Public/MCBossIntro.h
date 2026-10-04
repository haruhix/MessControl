#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCBossIntro.generated.h"

class AMCBossCharacter;
class AMCPlayerController;
class ACineCameraActor;
class ALevelSequenceActor;
class ULevelSequencePlayer;
class UPointLightComponent;

/** Local playback of an authored, five-second Sequencer camera move. Never spawns or activates a boss. */
UCLASS(NotBlueprintable)
class MESSCONTROL_API AMCBossIntro : public AActor
{
    GENERATED_BODY()
public:
    AMCBossIntro();
    bool PlayIntro(AMCPlayerController* Player,AMCBossCharacter* Boss);
    void CancelIntro();
protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UFUNCTION() void FinishIntro();
    void CheckIntro();
    UPROPERTY(Transient) TObjectPtr<ULevelSequencePlayer> SequencePlayer;
    UPROPERTY(Transient) TObjectPtr<ALevelSequenceActor> SequenceActor;
    UPROPERTY(Transient) TObjectPtr<ACineCameraActor> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> IntroFill;
    TWeakObjectPtr<AMCPlayerController> Controller;
    TWeakObjectPtr<AMCBossCharacter> Subject;
    TWeakObjectPtr<APawn> OriginalPawn;
    TWeakObjectPtr<AActor> PreviousViewTarget;
    FTimerHandle Watchdog;
    bool bPreviousAutoCamera=true;
    bool bPresentationStarted=false;
    bool bFinishing=false;
    double StartedAt=0;
};
