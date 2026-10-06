#include "MCMainMenuGameMode.h"
#include "MCMainMenuPlayerController.h"

AMCMainMenuGameMode::AMCMainMenuGameMode()
{
    PlayerControllerClass=AMCMainMenuPlayerController::StaticClass();
    DefaultPawnClass=nullptr;
    HUDClass=nullptr;
    bUseSeamlessTravel=false;
}
