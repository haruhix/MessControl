#include "MCPlayerController.h"
#include "MCFishingRewardWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCHubWidget.h"

void AMCPlayerController::PrepareFishingRewardPresentation()
{
    if(!HasLocalPresentationOwner() || !IsHubWorld()) return;
    if(!FishingRewardWidget) {
        FishingRewardWidget=CreateWidget<UMCFishingRewardWidget>(this,
            UMCGameplayUIFactory::ResolveClass(TEXT("FishingReward"),UMCFishingRewardWidget::StaticClass()));
        if(!FishingRewardWidget) return;
        FishingRewardWidget->Theme=HubWidget?HubWidget->GetResolvedTheme():UMCGameplayUIFactory::GetMasterTheme();
        FishingRewardWidget->BuildPresentationTree();FishingRewardWidget->ApplyTheme();
        // Above the Hub footer, below party/settings modals; never consumes input.
        FishingRewardWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
        FishingRewardWidget->AddToViewport(32);
        FishingRewardWidget->TakeWidget();FishingRewardWidget->ForceLayoutPrepass();
    }
    if(HubWidget) FishingRewardWidget->SetCounterTarget(
        HubWidget->GetWidgetFromName(TEXT("HubFishIcon")),HubWidget->GetWidgetFromName(TEXT("HubFishCard")));
}

void AMCPlayerController::ResetFishingRewardPresentation()
{
    if(FishingRewardWidget) {
        FishingRewardWidget->ClearPresentation();FishingRewardWidget->RemoveFromParent();
    }
    FishingRewardWidget=nullptr;
}

void AMCPlayerController::ClientFishingCatch_Implementation(EMCHubItemRarity Rarity,int32 FishCoins,int32 WalletAfter)
{
    if(!HasLocalPresentationOwner() || !IsHubWorld() || uint8(Rarity)>uint8(EMCHubItemRarity::Legendary)
        || FishCoins<0 || FishCoins>1000 || WalletAfter<0) return;
    PrepareFishingRewardPresentation();
    if(FishingRewardWidget) FishingRewardWidget->PresentCatch(Rarity,FishCoins,WalletAfter);
}
