#pragma once

#include "CoreMinimal.h"
#include "MCGameMode.h"
#include "MCPerkPreviewGameMode.generated.h"

/** Real native game-mode type, with run orchestration deliberately absent in the editor sandbox. */
UCLASS(Transient,NotBlueprintable)
class AMCPerkPreviewGameMode final : public AMCGameMode
{
    GENERATED_BODY()
public:
    AMCPerkPreviewGameMode();
    virtual void InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage) override;
    virtual void InitGameState() override;
    virtual void PreInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer,const FTransform& SpawnTransform) override;
    virtual void FinishRestartPlayer(AController* NewPlayer,const FRotator& StartRotation) override;
};
