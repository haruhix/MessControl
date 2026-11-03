#include "MCPlayerController.h"
#include "MCFishingRewardWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCHubWidget.h"
#include "MCUIPresentationSubsystem.h"
#include "Engine/GameInstance.h"

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
    RefreshFishingCounterTarget();
}
void AMCPlayerController::RefreshFishingCounterTarget()
{
    if(!FishingRewardWidget || !HubWidget) return;
    UWidget* Icon=HubWidget->GetWidgetFromName(TEXT("HubFishIcon"));
    UWidget* Card=HubWidget->GetWidgetFromName(TEXT("HubFishCard"));
    if(auto* GI=GetGameInstance()) if(auto* UI=GI->GetSubsystem<UMCUIPresentationSubsystem>()) {
        UWidget* NewIcon=nullptr;UWidget* NewCard=nullptr;
        if(UI->GetUnifiedFishCounterTargets(this,NewIcon,NewCard)) {Icon=NewIcon;Card=NewCard;}
    }
    FishingRewardWidget->SetCounterTarget(Icon,Card);
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
