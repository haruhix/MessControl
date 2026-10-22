#include "MCPlayerController.h"
#include "MCHubWardrobeStation.h"
#include "MCHubWardrobeWidget.h"
#include "MCHubShopStation.h"
#include "MCHubShopWidget.h"
#include "MCHubGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCPlayerState.h"
#include "MCGameUserSettings.h"
#include "MCGameplayUIFactory.h"
#include "MCFishingRodComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

bool AMCPlayerController::IsWardrobeOpen() const
{
    return WardrobeWidget && WardrobeWidget->IsInViewport() && WardrobeWidget->IsVisible();
}

bool AMCPlayerController::IsWardrobeAvailable() const
{
    const auto* Hub=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    return IsLocalController() && Hub && Hub->Phase==EMCHubPhase::Gathering
        && !IsWardrobeOpen() && !IsHubShopOpen() && NearbyWardrobe.IsValid() && NearbyWardrobe->ContainsPawn(GetPawn());
}

bool AMCPlayerController::IsHubShopOpen() const
{return HubShopWidget && HubShopWidget->IsInViewport() && HubShopWidget->IsVisible();}

bool AMCPlayerController::IsHubShopAvailable() const
{
    const auto* Hub=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    return IsLocalController() && Hub && Hub->Phase==EMCHubPhase::Gathering && !IsWardrobeOpen() && !IsHubShopOpen()
        && NearbyShop.IsValid() && NearbyShop->ContainsPawn(GetPawn());
}

bool AMCPlayerController::TryOpenHubShop()
{
    if(!IsLocalController() || !IsHubWorld() || bLoadingInputBlocked || bHubMenuInput || bPauseMenuOpen
        || bConnectionMenuOpen || bLobbyUIOpen || bBossIntroPlaying || IsRewardInteractionActive() || IsWardrobeOpen()) return false;
    if(IsHubShopOpen()) return true;
    NextWardrobeCheck=0;RefreshWardrobe();
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!Hero || !Hero->CanWork() || !IsHubShopAvailable()) return false;
    if(!HubShopWidget) {
        HubShopWidget=CreateWidget<UMCHubShopWidget>(this,
            UMCGameplayUIFactory::ResolveClass(TEXT("Shop"),UMCHubShopWidget::StaticClass()));
        if(!HubShopWidget) return false;
        HubShopWidget->OnClose.AddDynamic(this,&ThisClass::CloseHubShop);
    }
    // The two hub modals are mutually exclusive and share this saved orbit.
    WardrobePreviousYaw=Hero->CameraOrbitYaw;WardrobePreviousPitch=Hero->CameraOrbitPitch;
    WardrobePreviousDistance=Hero->CameraOrbitDistance;
    WardrobePawn=Hero;
    Hero->CancelGameplayInput();
    Hero->SetCameraOrbitView(float(NearbyShop->GetActorRotation().Yaw),-12.f,900.f);
    HubShopWidget->SetVisibility(ESlateVisibility::Visible);HubShopWidget->AddToViewport(90);
    HideScoreboard();UpdateInputMode();HubShopWidget->SetUserFocus(this);return true;
}

void AMCPlayerController::CloseHubShop()
{
    if(IsHubShopOpen()) if(auto* Hero=WardrobePawn.Get();Hero && Hero==GetPawn())
        Hero->SetCameraOrbitView(WardrobePreviousYaw,WardrobePreviousPitch,WardrobePreviousDistance);
    if(IsHubShopOpen()) WardrobePawn.Reset();
    if(HubShopWidget) HubShopWidget->RemoveFromParent();
    if(IsLocalController() && GetWorld() && !GetWorld()->bIsTearingDown) UpdateInputMode();
}

void AMCPlayerController::ClientSetHubArrivalView_Implementation(float Yaw)
{
    if(!FMath::IsFinite(Yaw)) return;
    HubArrivalYaw=FRotator::NormalizeAxis(Yaw);
    HubArrivalViewPawn.Reset();
    // Possession can reach the owning client after this RPC. RefreshWardrobe
    // applies the initial view to each newly received pawn, never each frame.
}

void AMCPlayerController::RefreshWardrobe()
{
    if(!IsLocalController() || !GetWorld()) return;
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>();
    if(Hero && Hub && !IsWardrobeOpen() && !IsHubShopOpen() && HubArrivalViewPawn.Get()!=Hero) {
        const float Yaw=HubArrivalYaw.IsSet()?HubArrivalYaw.GetValue():float(Hero->GetActorRotation().Yaw);
        Hero->SetCameraOrbitView(Yaw,-12.f,Hero->CameraOrbitDistance);
        HubArrivalViewPawn=Hero;
    }
    const double Now=GetWorld()->GetRealTimeSeconds();
    if(SavedAppearancePawn.Get()!=Hero) {
        SavedAppearancePawn=Hero;bSavedAppearanceApplied=false;ApplySavedAppearanceAt=Now+.3;
    }
    if(Hero && GetPlayerState<AMCPlayerState>() && !bLoadingInputBlocked && !bSavedAppearanceApplied && Now>=ApplySavedAppearanceAt) {
        bSavedAppearanceApplied=true;
        if(const auto* Settings=UMCGameUserSettings::Get();Settings && Settings->HasCustomBagColor())
            ServerSetPlayerColor(Settings->GetSavedBagColor());
    }
    if(IsWardrobeOpen()) {
        if(!Hero || WardrobePawn.Get()!=Hero || !Hub || Hub->Phase!=EMCHubPhase::Gathering
            || bLoadingInputBlocked || !NearbyWardrobe.IsValid() || !NearbyWardrobe->ContainsPawn(Hero)
            || !Hero->Status || !Hero->Status->IsAlive()) {CloseWardrobe();return;}
        Hero->PreviewBagColor(WardrobeWidget->GetDraftColor());
    }
    if(IsHubShopOpen() && (!Hero || !Hub || Hub->Phase!=EMCHubPhase::Gathering || bLoadingInputBlocked
        || !NearbyShop.IsValid() || !NearbyShop->ContainsPawn(Hero))) CloseHubShop();
    const bool ShowPrompt=Hero && Hub && Hub->Phase==EMCHubPhase::Gathering && Hero->CanWork()
        && !bLoadingInputBlocked && !bHubMenuInput && !bPauseMenuOpen && !bConnectionMenuOpen
        && !bLobbyUIOpen && !bBossIntroPlaying && !IsRewardInteractionActive() && !IsWardrobeOpen() && !IsHubShopOpen();
    if(NearbyWardrobe.IsValid()) NearbyWardrobe->SetLocalPromptVisible(this,ShowPrompt);
    if(NearbyShop.IsValid()) NearbyShop->SetLocalPromptVisible(this,ShowPrompt);
    if(Now<NextWardrobeCheck) return;
    NextWardrobeCheck=Now+.1;
    if(NearbyWardrobe.IsValid()) NearbyWardrobe->SetLocalPromptVisible(this,false);
    if(NearbyShop.IsValid()) NearbyShop->SetLocalPromptVisible(this,false);
    NearbyWardrobe.Reset();NearbyShop.Reset();
    if(!Hero || !Hub || Hub->Phase!=EMCHubPhase::Gathering) return;
    double Nearest=TNumericLimits<double>::Max();
    for(TActorIterator<AMCHubWardrobeStation> It(GetWorld());It;++It) {
        const double Distance=FVector::DistSquared(Hero->GetActorLocation(),It->GetActorLocation());
        if(Distance<Nearest && It->ContainsPawn(Hero)) {NearbyWardrobe=*It;Nearest=Distance;}
    }
    Nearest=TNumericLimits<double>::Max();
    for(TActorIterator<AMCHubShopStation> It(GetWorld());It;++It) {
        const double Distance=FVector::DistSquared(Hero->GetActorLocation(),It->GetActorLocation());
        if(Distance<Nearest && It->ContainsPawn(Hero)) {NearbyShop=*It;Nearest=Distance;}
    }
    if(NearbyWardrobe.IsValid()) NearbyWardrobe->SetLocalPromptVisible(this,ShowPrompt);
    if(NearbyShop.IsValid()) NearbyShop->SetLocalPromptVisible(this,ShowPrompt);
}

bool AMCPlayerController::TryOpenWardrobe()
{
    if(!IsLocalController() || !IsHubWorld() || bLoadingInputBlocked || bHubMenuInput || bPauseMenuOpen
        || bConnectionMenuOpen || bLobbyUIOpen || bBossIntroPlaying || IsRewardInteractionActive() || IsHubShopOpen()) return false;
    if(IsWardrobeOpen()) return true;
    NextWardrobeCheck=0;RefreshWardrobe();
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!Hero || !Hero->CanWork() || !IsWardrobeAvailable()) return false;
    if(!WardrobeWidget) {
        WardrobeWidget=CreateWidget<UMCHubWardrobeWidget>(this,
            UMCGameplayUIFactory::ResolveClass(TEXT("Wardrobe"),UMCHubWardrobeWidget::StaticClass()));
        if(!WardrobeWidget) return false;
        WardrobeWidget->OnDraftChanged.AddDynamic(this,&ThisClass::PreviewWardrobeColor);
        WardrobeWidget->OnApply.AddDynamic(this,&ThisClass::ApplyWardrobeColor);
        WardrobeWidget->OnCancel.AddDynamic(this,&ThisClass::CancelWardrobe);
    }
    WardrobePawn=Hero;
    WardrobePreviousYaw=Hero->CameraOrbitYaw;WardrobePreviousPitch=Hero->CameraOrbitPitch;
    WardrobePreviousDistance=Hero->CameraOrbitDistance;
    Hero->CancelGameplayInput();
    if(Hero->FishingRod && Hero->FishingRod->IsPresented()) Hero->FishingRod->ServerSetEquipped(false);
    // Look toward the booth from its open approach, so the camera cannot be
    // pushed into the character by the booth collision. Keep the bag in view.
    Hero->SetCameraOrbitView(float(NearbyWardrobe->GetActorRotation().Yaw)-90.f,-10.f,650.f);
    WardrobeWidget->Configure(Hero->GetPlayerColor());
    WardrobeWidget->SetVisibility(ESlateVisibility::Visible);WardrobeWidget->AddToViewport(90);
    HideScoreboard();UpdateInputMode();WardrobeWidget->SetUserFocus(this);
    return true;
}

void AMCPlayerController::PreviewWardrobeColor(FLinearColor Color)
{
    if(IsWardrobeOpen()) if(auto* Hero=WardrobePawn.Get()) Hero->PreviewBagColor(Color);
}

void AMCPlayerController::ApplyWardrobeColor(FLinearColor Color)
{
    auto* Hero=WardrobePawn.Get();
    if(!IsWardrobeOpen() || !Hero || !NearbyWardrobe.IsValid() || !NearbyWardrobe->ContainsPawn(Hero)) return;
    if(!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)) return;
    Color=Color.GetClamped(0,1);Color.A=1;
    // One authoritative commit, not one reliable RPC per slider tick.
    ServerSetPlayerColor(Color);
    if(auto* Settings=UMCGameUserSettings::Get()) {Settings->SetSavedBagColor(Color);Settings->SaveSettings();}
    CloseWardrobe(false);
}

void AMCPlayerController::CancelWardrobe() {CloseWardrobe();}

void AMCPlayerController::FlushPlayerColor()
{
    if(!HasAuthority() || !GetWorld() || !PendingPlayerColor.IsSet()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<NextColorAt) return;
    auto* State=GetPlayerState<AMCPlayerState>();
    if(!State) return;
    const FLinearColor Color=PendingPlayerColor.GetValue();PendingPlayerColor.Reset();NextColorAt=Now+.2;
    State->PlayerColor=Color;State->ForceNetUpdate();
    if(auto* Hero=Cast<AMCToothCharacter>(GetPawn())) Hero->ApplyPlayerColor(Color);
}

void AMCPlayerController::CloseWardrobe(bool bRestoreColor)
{
    if(auto* Hero=WardrobePawn.Get()) {
        if(bRestoreColor) Hero->PreviewBagColor(Hero->GetPlayerColor());
        Hero->SetCameraOrbitView(WardrobePreviousYaw,WardrobePreviousPitch,WardrobePreviousDistance);
    }
    WardrobePawn.Reset();
    if(WardrobeWidget) WardrobeWidget->RemoveFromParent();
    if(IsLocalController() && GetWorld() && !GetWorld()->bIsTearingDown) UpdateInputMode();
}
