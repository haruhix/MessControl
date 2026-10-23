#include "MCPlayerController.h"
#include "MCHubEconomyComponent.h"
#include "MCHubProfileSubsystem.h"
#include "MCHubGrillStation.h"
#include "MCHubShopStation.h"
#include "MCHubWardrobeStation.h"
#include "MCHubGameState.h"
#include "MCHubWidget.h"
#include "MCHubShopWidget.h"
#include "MCHubWardrobeWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCCharacterCosmeticsComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCPlayerState.h"
#include "MCCalciumGrenade.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"

void AMCPlayerController::RefreshHubEconomy()
{
    auto* State=GetPlayerState<AMCPlayerState>();
    if(!HasAuthority() || !State || !State->Economy || !GetGameInstance() || !GetWorld()->GetGameState()) return;
    auto* Profiles=GetGameInstance()->GetSubsystem<UMCHubProfileSubsystem>();
    if(Profiles && !Profiles->Restore(State,this)) return;
    if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();Mode && Mode->GetHubBoosterRunId().IsValid()) {
        FText Error;State->Economy->ApplyPendingBoostersForRun(Mode->GetHubBoosterRunId(),Error);
    }
    if(auto* Hero=Cast<AMCToothCharacter>(GetPawn());Hero && Hero->Cosmetics
        && !(Hero->Cosmetics->EquippedLoadout==State->Economy->EquippedLoadout))
        Hero->Cosmetics->SetEquippedLoadout(State->Economy->EquippedLoadout);
    const double Now=GetWorld()->GetRealTimeSeconds();
    if(Profiles && Now>=NextHubProfileSaveAt) {NextHubProfileSaveAt=Now+2.;Profiles->Store(State,this);}
}

bool AMCPlayerController::TryInteractHubGrill()
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!IsLocalController() || !IsHubWorld() || !Hero || !Hero->CanWork()
        || bLoadingInputBlocked || bHubMenuInput || bPauseMenuOpen || IsWardrobeOpen() || IsHubShopOpen()) return false;
    AMCHubGrillStation* Nearest=nullptr;double Distance=TNumericLimits<double>::Max();
    for(TActorIterator<AMCHubGrillStation> It(GetWorld());It;++It) if(It->ContainsPawn(Hero)) {
        const double D=FVector::DistSquared(It->GetActorLocation(),Hero->GetActorLocation());
        if(D<Distance) {Nearest=*It;Distance=D;}
    }
    if(!Nearest) return false;
    ServerInteractHubGrill(Nearest);return true;
}
void AMCPlayerController::ServerInteractHubGrill_Implementation(AMCHubGrillStation* Station)
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(!IsValid(Station) || Station->GetWorld()!=GetWorld() || !Hero || !Hero->CanWork()) return;
    FText Error;
    if(!Station->TryStartRoll(Hero,Error) && !Error.IsEmpty()) ClientHubTransactionFeedback(Error);
}
void AMCPlayerController::PurchaseHubItem(FName ItemID)
{
    if(IsLocalController() && (IsHubShopOpen() || IsWardrobeOpen())) ServerPurchaseHubItem(ItemID);
}
void AMCPlayerController::ServerPurchaseHubItem_Implementation(FName ItemID)
{
    auto* State=GetPlayerState<AMCPlayerState>();auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>();
    if(!State || !State->Economy || !Hero || !Hero->CanWork() || !Hub || Hub->Phase!=EMCHubPhase::Gathering) return;
    const auto* Item=State->Economy->FindCatalogItem(ItemID);if(!Item) return;
    bool Nearby=false;
    if(Item->Kind==EMCHubCatalogKind::Cosmetic) {
        for(TActorIterator<AMCHubWardrobeStation> It(GetWorld());It;++It) if(It->ContainsPawn(Hero)) {Nearby=true;break;}
    } else for(TActorIterator<AMCHubShopStation> It(GetWorld());It;++It) if(It->ContainsPawn(Hero)) {Nearby=true;break;}
    if(!Nearby) return;
    FText Error;
    if(!State->Economy->TryPurchase(ItemID,Error)) {if(!Error.IsEmpty()) ClientHubTransactionFeedback(Error);}
    else ClientHubTransactionFeedback(NSLOCTEXT("MCHubEconomy","Purchased","Покупка сохранена."));
}
void AMCPlayerController::ClientHubTransactionFeedback_Implementation(const FText& Message)
{
    if(IsHubShopOpen() && HubShopWidget) HubShopWidget->SetTransactionMessage(Message);
    else if(IsWardrobeOpen() && WardrobeWidget) WardrobeWidget->SetTransactionMessage(Message);
    else if(HubWidget) HubWidget->ShowInteractionFeedback(Message);
}
void AMCPlayerController::UseCalciumGrenade()
{
    const auto* State=GetPlayerState<AMCPlayerState>();
    if(!State || !State->Economy || !State->Economy->IsCalciumEnabled()
        || !IsLocalController() || IsHubWorld() || bLoadingInputBlocked || bPauseMenuOpen || bHubMenuInput
        || IsRewardInteractionActive() || IsWardrobeOpen() || IsHubShopOpen()) return;
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());
    if(Hero && Hero->CanWork() && !IsMoveInputIgnored()) ServerUseCalciumGrenade();
}
void AMCPlayerController::ServerEquipHubCosmetics_Implementation(FMCCharacterCosmeticLoadout Loadout)
{
    const auto* Hub=GetWorld()->GetGameState<AMCHubGameState>();
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());auto* State=GetPlayerState<AMCPlayerState>();
    if(!Hub || Hub->Phase!=EMCHubPhase::Gathering || !Hero || !Hero->CanWork() || !State || !State->Economy) return;
    bool Nearby=false;
    for(TActorIterator<AMCHubWardrobeStation> It(GetWorld());It;++It) if(It->ContainsPawn(Hero)) {Nearby=true;break;}
    if(!Nearby) return;
    FText Error;
    if(!State->Economy->TryEquipLoadout(Loadout,Error)) {
        if(!Error.IsEmpty()) ClientHubTransactionFeedback(Error);return;
    }
    if(Hero->Cosmetics) Hero->Cosmetics->SetEquippedLoadout(State->Economy->EquippedLoadout);
}
void AMCPlayerController::ServerUseCalciumGrenade_Implementation()
{
    auto* Hero=Cast<AMCToothCharacter>(GetPawn());auto* State=GetPlayerState<AMCPlayerState>();
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if(!Hero || !Hero->CanWork() || !Hero->Status || !State || !State->Economy || !State->Economy->IsCalciumEnabled() || !Game
        || Game->bTutorialActive || Game->bLobbyWaiting || Game->Phase!=EMCShiftPhase::Working
        || Hero->Status->State.Health>=Hero->Status->State.MaxHealth) return;
    FHitResult Floor;
    const FVector Origin=Hero->GetActorLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCalciumUnderfoot),false,Hero);
    if(!GetWorld()->LineTraceSingleByChannel(Floor,Origin,Origin-FVector(0,0,300),ECC_Visibility,Query)
        || Floor.ImpactNormal.Z<.5f) return;
    if(!State->Economy->TryConsumeCalciumCharge()) return;
    auto* Grenade=GetWorld()->SpawnActorDeferred<AMCCalciumGrenade>(AMCCalciumGrenade::StaticClass(),
        FTransform(Floor.ImpactPoint+FVector(0,0,4)),Hero,Hero,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Grenade) {State->Economy->RefundCalciumCharge();return;}
    UGameplayStatics::FinishSpawningActor(Grenade,Grenade->GetActorTransform());
}
