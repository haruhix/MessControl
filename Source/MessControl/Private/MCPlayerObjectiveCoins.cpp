#include "MCPlayerController.h"
#include "MCTaskCoinPresentationComponent.h"

void AMCPlayerController::PrepareTaskCoinPresentation()
{
    if(!HasLocalPresentationOwner() || IsHubWorld()) return;
    if(!TaskCoinPresentation) {
        TaskCoinPresentation=NewObject<UMCTaskCoinPresentationComponent>(this,TEXT("ObjectiveCoinPresentation"));
        AddInstanceComponent(TaskCoinPresentation);
        TaskCoinPresentation->RegisterComponent();
    }
    TaskCoinPresentation->PreparePresentation();
}

void AMCPlayerController::ResetTaskCoinPresentation()
{
    if(TaskCoinPresentation) {
        TaskCoinPresentation->ResetPresentation();
        TaskCoinPresentation->DestroyComponent();
    }
    TaskCoinPresentation=nullptr;
}

void AMCPlayerController::ClientObjectiveCoins_Implementation(int32 Amount,int32 WalletAfter)
{
    if(!HasLocalPresentationOwner() || IsHubWorld() || Amount<=0 || Amount>1000 || WalletAfter<Amount) return;
    // No late loading here. A dead/loading player still owns the authoritative
    // coins; missing cosmetics must never delay credit or create a pickup lock.
    if(TaskCoinPresentation && TaskCoinPresentation->HasPreparedPresentation())
        TaskCoinPresentation->PresentCoins(Amount,WalletAfter);
}
