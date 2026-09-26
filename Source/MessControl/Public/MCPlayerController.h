#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MCDevCommands.h"
#include "MCPlayerController.generated.h"
class UMCPrototypeWidget;
class UMCDevPanelWidget;
class UMCEmoteWidget;

UCLASS()
class MESSCONTROL_API AMCPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    void ToggleDevPanel();
    void ToggleEmotes();
    bool CanUseDevPanel() const;
    void RequestDevAction(EMCDevAction Action,int32 StepIndex=INDEX_NONE);
    void ToggleTuning();
    void ToggleConnection();
    void UpdateInputMode();
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void HostGame();
    UFUNCTION(BlueprintCallable, Category="Multiplayer") void JoinGame(const FString& Address);
    UFUNCTION(BlueprintCallable, Category="Shift") void RequestRestart();
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UMCPrototypeWidget> PrototypeWidget;
    UPROPERTY() TObjectPtr<UMCDevPanelWidget> DevPanel;
    UPROPERTY() TObjectPtr<UMCEmoteWidget> EmoteWidget;
private:
    UFUNCTION(Server, Reliable) void ServerRestartShift();
};
