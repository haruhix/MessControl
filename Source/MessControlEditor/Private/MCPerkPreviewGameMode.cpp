#include "MCPerkPreviewGameMode.h"

#include "MCGameState.h"
#include "MCPlayerState.h"
#include "GameFramework/PlayerController.h"

AMCPerkPreviewGameMode::AMCPerkPreviewGameMode()
{
    GameStateClass=AMCGameState::StaticClass();PlayerStateClass=AMCPlayerState::StaticClass();
    PlayerControllerClass=APlayerController::StaticClass();DefaultPawnClass=nullptr;HUDClass=nullptr;
    bStartPlayersAsSpectators=true;bUseDayOnePlan=false;bUseSingleDayLoop=false;bUseAdaptiveDirector=false;
    RunRulesProfile.Reset();FirstDayPlan.Reset();DirectorProfile.Reset();SingleDayProfile.Reset();
    PrimaryActorTick.bStartWithTickEnabled=false;
}
void AMCPerkPreviewGameMode::InitGame(const FString& Map,const FString& Options,FString& Error)
{AGameModeBase::InitGame(Map,Options,Error);}
void AMCPerkPreviewGameMode::InitGameState(){AGameModeBase::InitGameState();}
void AMCPerkPreviewGameMode::PreInitializeComponents(){AGameModeBase::PreInitializeComponents();}
void AMCPerkPreviewGameMode::BeginPlay(){AGameModeBase::BeginPlay();}
void AMCPerkPreviewGameMode::StartPlay(){AGameModeBase::StartPlay();}
void AMCPerkPreviewGameMode::Tick(float DeltaSeconds){AGameModeBase::Tick(DeltaSeconds);}
void AMCPerkPreviewGameMode::PreLogin(const FString&,const FString&,const FUniqueNetIdRepl&,FString& Error){Error.Reset();}
void AMCPerkPreviewGameMode::PostLogin(APlayerController*){}
void AMCPerkPreviewGameMode::HandleStartingNewPlayer_Implementation(APlayerController*){}
APawn* AMCPerkPreviewGameMode::SpawnDefaultPawnAtTransform_Implementation(AController*,const FTransform&){return nullptr;}
void AMCPerkPreviewGameMode::FinishRestartPlayer(AController*,const FRotator&){}
