#include "MCFrontEndValidationSubsystem.h"
#include "MCMainMenuGameMode.h"
#include "MCMainMenuPlayerController.h"
#include "MCMainMenuWidget.h"
#include "MCTutorialDirector.h"
#include "MCTutorialWidget.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothCalculusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCArenaTooth.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

namespace MCFrontEndSmoke
{
    uint32 RequiredTutorialStages()
    {
        const EMCTutorialStage Stages[]={EMCTutorialStage::Intro,EMCTutorialStage::BrushTooth,EMCTutorialStage::FoodCut,
            EMCTutorialStage::FreshSort,EMCTutorialStage::SpoiledSort,EMCTutorialStage::BreakfastRain,
            EMCTutorialStage::BreakfastCleanup,EMCTutorialStage::CoffeeWarning,EMCTutorialStage::CoffeeWaves,
            EMCTutorialStage::CoffeeCleanup,EMCTutorialStage::Calculus,EMCTutorialStage::FoamParty,EMCTutorialStage::Complete};
        uint32 Mask=0;
        for (const auto Stage:Stages) Mask|=1u << static_cast<uint8>(Stage);
        return Mask;
    }
    bool DefaultToolsAvailable(AMCToothCharacter* Hero)
    {
        if (!Hero || !Hero->Inventory || Hero->EquippedBrush || !Hero->HasBrush()) return false;
        const EMCToolSlot Slots[]={EMCToolSlot::Pickaxe,EMCToolSlot::Knife,EMCToolSlot::Spray,EMCToolSlot::Brush};
        for (const auto Slot:Slots)
        {
            Hero->Inventory->ServerSelect(Slot);
            if (Hero->Inventory->Selected!=Slot) return false;
        }
        return Hero->HasBrush();
    }
    FString Caption(UWidget* Widget)
    {
        if (const auto* Text=Cast<UTextBlock>(Widget)) return Text->GetText().ToString();
        if (const auto* Panel=Cast<UPanelWidget>(Widget))
            for (int32 I=0;I<Panel->GetChildrenCount();++I)
            {
                FString Text=Caption(Panel->GetChildAt(I)); if (!Text.IsEmpty()) return Text;
            }
        return FString();
    }
    bool Click(UMCMainMenuWidget* Menu,const TCHAR* Text)
    {
        if (!Menu || !Menu->WidgetTree) return false;
        TArray<UWidget*> Widgets; Menu->WidgetTree->GetAllWidgets(Widgets);
        for (UWidget* Widget:Widgets)
            if (auto* Button=Cast<UButton>(Widget); Button && Caption(Button)==Text && Button->GetIsEnabled())
            { Button->OnClicked.Broadcast(); return true; }
        return false;
    }
}

bool UMCFrontEndValidationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    return Super::ShouldCreateSubsystem(Outer) && (FParse::Param(FCommandLine::Get(),TEXT("MCFrontEndSmoke")) || FParse::Param(FCommandLine::Get(),TEXT("MCTutorialSmoke")));
#endif
}

void UMCFrontEndValidationSubsystem::Finish(bool bSuccess,const FString& Reason)
{
    if (bFinished) return; bFinished=true;
    const bool Menu=FParse::Param(FCommandLine::Get(),TEXT("MCFrontEndSmoke"));
    if (bSuccess) { UE_LOG(LogTemp,Display,TEXT("MC_%s_PASS net=%d %s"),Menu?TEXT("FRONTEND"):TEXT("TUTORIAL"),static_cast<int32>(GetWorld()->GetNetMode()),*Reason); }
    else { UE_LOG(LogTemp,Error,TEXT("MC_%s_FAIL net=%d stage=%d %s"),Menu?TEXT("FRONTEND"):TEXT("TUTORIAL"),static_cast<int32>(GetWorld()->GetNetMode()),static_cast<int32>(LastStage),*Reason); }
    FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
}

void UMCFrontEndValidationSubsystem::Capture(const FString& Name)
{
    if (bCaptured || !FParse::Param(FCommandLine::Get(),TEXT("MCFrontEndCapture")) || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("FrontEndSmoke");
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Path=Directory/(Name+FString::Printf(TEXT("_Peer%d.png"),static_cast<int32>(GetWorld()->GetNetMode())));
    FScreenshotRequest::RequestScreenshot(Path,true,false); bCaptured=true;
    UE_LOG(LogTemp,Display,TEXT("MC_FRONTEND_CAPTURE %s"),*Path);
}

void UMCFrontEndValidationSubsystem::Tick(float)
{
#if !UE_BUILD_SHIPPING
    if (bFinished) return;
    if (StartedAt==0)
    {
        StartedAt=FPlatformTime::Seconds();
        FParse::Value(FCommandLine::Get(),TEXT("MCTutorialSmokePlayers="),ExpectedPeers); ExpectedPeers=FMath::Clamp(ExpectedPeers,1,4);
    }
    if (FPlatformTime::Seconds()-StartedAt>115)
    { Finish(false,FString::Printf(TEXT("timeout roster/stage/widget: peers=%d seen=0x%x ui=%d"),ExpectedPeers,SeenStages,bSawWidget)); return; }
    if (FParse::Param(FCommandLine::Get(),TEXT("MCFrontEndSmoke"))) TickMenu(); else TickTutorial();
#endif
}

void UMCFrontEndValidationSubsystem::TickMenu()
{
    const double Age=FPlatformTime::Seconds()-StartedAt;
    if (Age<NextMenuAt) return;
    if (!GetWorld()->GetAuthGameMode<AMCMainMenuGameMode>() || !Cast<AMCMainMenuPlayerController>(GetWorld()->GetFirstPlayerController()))
    { Finish(false,TEXT("Main menu has the wrong game mode/controller")); return; }
    if (GetWorld()->GetGameState<AMCGameState>() || GetWorld()->GetFirstPlayerController()->GetPawn())
    { Finish(false,TEXT("Main menu unexpectedly spawned the mouth game state or pawn")); return; }
    TArray<UUserWidget*> Widgets; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,UMCMainMenuWidget::StaticClass(),true);
    if (Widgets.IsEmpty()) { if (Age>8) Finish(false,TEXT("Main menu widget did not appear")); return; }
    auto* Menu=Cast<UMCMainMenuWidget>(Widgets[0]);
    if (MenuStep==0) { Capture(TEXT("MainMenu")); ++MenuStep; NextMenuAt=Age+1; return; }
    const TCHAR* Sequence[]={TEXT("Settings"),TEXT("Back"),TEXT("Credits"),TEXT("Back"),TEXT("Join Lobby"),TEXT("Back"),TEXT("Create Lobby"),TEXT("Back"),TEXT("Exit"),TEXT("Cancel")};
    if (MenuStep<=UE_ARRAY_COUNT(Sequence))
    {
        const TCHAR* Button=Sequence[MenuStep-1];
        if (!MCFrontEndSmoke::Click(Menu,Button)) { Finish(false,FString::Printf(TEXT("No enabled navigation button: %s"),Button)); return; }
        UE_LOG(LogTemp,Display,TEXT("MC_FRONTEND_NAVIGATION %s"),Button); ++MenuStep; NextMenuAt=Age+.5; return;
    }
    Finish(true,TEXT("Menu, Settings, Credits, Join, Create and Exit/Cancel navigation work; no gameplay spawned"));
}

bool UMCFrontEndValidationSubsystem::Deliver(AMCFoodActor* Food,AMCToothCharacter* Hero)
{
    if (!IsValid(Food) || !Hero || Food->IsDisposed()) return false;
    if (Food->Phase==EMCFoodPhase::Swallowing) return true;
    AMCThroat* Throat=nullptr;
    for (TActorIterator<AMCThroat> It(GetWorld());It;++It) { Throat=*It; break; }
    if (!Throat) return false;
    // Establish attribution through the same authoritative carry helper used by collection.
    Food->SetStackCarrier(Hero); Food->SetStackCarrier(nullptr);
    Food->SetActorLocation(Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter+FVector(0,0,60)),false,nullptr,ETeleportType::TeleportPhysics);
    Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    return Throat->AcceptDelivery(Food);
}

bool UMCFrontEndValidationSubsystem::ClearCalculus(AMCTutorialDirector* Director,AMCToothCharacter* Hero,AMCArenaTooth* Tooth)
{
    if (!Tooth || !Tooth->Calculus || Tooth->Calculus->State.Anchors.IsEmpty()) return false;
    Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
    if (Hero->Inventory->Selected!=EMCToolSlot::Pickaxe) return false;
    const auto& Anchor=Tooth->Calculus->State.Anchors[0];
    const FTransform Pose=Tooth->Visual->GetComponentTransform();
    const FVector Point=Pose.TransformPosition(Anchor.Center);
    const FVector Normal=Pose.TransformVectorNoScale(Anchor.Normal).GetSafeNormal();
    FVector Position=Point+Normal*75-FVector(0,0,65);
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
    {
        FHitResult Floor; if (It->SurfacePoint(Position,Floor)) { Position.Z=Floor.ImpactPoint.Z+54; break; }
    }
    Hero->CancelGameplayInput(); Hero->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    Hero->SetActorRotation((-Normal).GetSafeNormal2D().Rotation()); Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->bInCoffee=false;
    FVector Contact,ContactNormal;
    if (!Tooth->Calculus->FindContact(Hero,Contact,ContactNormal)) return false;
    if (!Tooth->Calculus->ApplyPickaxeHit(Hero,Contact,ContactNormal,75)) return false;
    if (!Tooth->Calculus->HasCalculus()) Director->NotifyAction(Hero,EMCTutorialAction::CalculusCleared,Tooth);
    return true;
}

void UMCFrontEndValidationSubsystem::TickTutorial()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if (!GS) return;
    TArray<UUserWidget*> Widgets; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,UMCTutorialWidget::StaticClass(),true);
    bSawWidget|=!Widgets.IsEmpty();
    auto* Director=AMCTutorialDirector::Find(GetWorld());
    if (!GS->bTutorialActive)
    {
        if (!bSawTutorial || !bSawComplete) return;
        if (Director) { Finish(false,TEXT("Tutorial director remains active after completion")); return; }
        if (GS->Day>=1 && GS->DayPlan && GS->Phase==EMCShiftPhase::Working)
        {
            const uint32 MissingStages=MCFrontEndSmoke::RequiredTutorialStages() & ~SeenStages;
            if (GetWorld()->GetNetMode()!=NM_Client && MissingStages!=0)
            { Finish(false,FString::Printf(TEXT("Day zero skipped required lessons: missing=0x%x seen=0x%x"),MissingStages,SeenStages)); return; }
            if (FinishedAt==0) FinishedAt=FPlatformTime::Seconds();
            if (FPlatformTime::Seconds()-FinishedAt>(GetWorld()->GetNetMode()==NM_Client?.5:2.5))
                Finish(bSawWidget && bCoffeeSafetyChecked,TEXT("Default tools, surviving lessons, team readiness and replication passed; ordinary day one resumed"));
        }
        return;
    }
    bSawTutorial=true;
    if (InitialMouthHealth<0) InitialMouthHealth=GS->MouthHealth;
    if (GS->MouthHealth<InitialMouthHealth-.01f) { Finish(false,TEXT("Tutorial reduced mouth health")); return; }
    if (!Director) return;
    if (Director->Stage!=LastStage)
    {
        LastStage=Director->Stage; StageSeenAt=FPlatformTime::Seconds(); SeenStages|=1u << static_cast<uint8>(LastStage);
        UE_LOG(LogTemp,Display,TEXT("MC_TUTORIAL_OBSERVED stage=%d peers=%d progress=%d/%d"),static_cast<int32>(LastStage),Director->GetRequiredPlayers(),Director->GetCompletedPlayers(),Director->GetRequiredPlayers());
        if (LastStage==EMCTutorialStage::Intro && GetWorld()->GetNetMode()!=NM_Client)
            for (const auto& Learner:Director->Players)
                if (!MCFrontEndSmoke::DefaultToolsAvailable(Learner.PlayerState?Cast<AMCToothCharacter>(Learner.PlayerState->GetPawn()):nullptr))
                { Finish(false,TEXT("A learner does not start with four usable inventory tools")); return; }
    }
    if (LastStage==EMCTutorialStage::TrashSort)
    { Finish(false,TEXT("Day zero entered the retired tool-disposal lesson")); return; }
    const double StageAge=FPlatformTime::Seconds()-StageSeenAt;
    if (LastStage==EMCTutorialStage::BrushTooth && StageAge>1) Capture(TEXT("Tutorial"));
    if (LastStage==EMCTutorialStage::BrushTooth && StageAge>2 && bCaptured
        && FParse::Param(FCommandLine::Get(),TEXT("MCFrontEndCaptureOnly")))
    {
        bFinished=true;
        UE_LOG(LogTemp,Display,TEXT("MC_UI_CAPTURE_PASS portrait preview only; full gameplay smoke was not run"));
        FPlatformMisc::RequestExitWithStatus(false,0);
        return;
    }
    if (LastStage==EMCTutorialStage::CoffeeWaves)
    {
        bCoffeeSafetyChecked=true;
        if (GetWorld()->GetNetMode()!=NM_Client)
            for (const auto& Learner:Director->Players)
                if (auto* Hero=Learner.PlayerState?Cast<AMCToothCharacter>(Learner.PlayerState->GetPawn()):nullptr)
                {
                    const float Health=Hero->Status->State.Health;
                    if (Hero->Status->Damage(100) || !FMath::IsNearlyEqual(Health,Hero->Status->State.Health))
                    { Finish(false,TEXT("Coffee tutorial did not guard real player damage")); return; }
                }
    }
    if (LastStage==EMCTutorialStage::Complete && !bSawComplete)
    {
        bCaptured=false;
        bSawComplete=true;
    }
    if (LastStage==EMCTutorialStage::Complete && StageAge>.4) Capture(TEXT("TutorialReady"));
    if (GetWorld()->GetNetMode()==NM_Client) return;
    if (Director->GetRequiredPlayers()!=ExpectedPeers || !FMCTutorialProgressRules::AllLoaded(Director->Players)) return;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if (It->Batch==AMCTutorialDirector::TutorialFoodBatch && It->bBrushTool)
        { Finish(false,TEXT("Day zero spawned a physical tool pickup")); return; }
    for (const auto& Learner:Director->Players)
        if (Learner.PlayerState && Learner.PlayerState->Points!=0) { Finish(false,TEXT("Tutorial actions leaked into ordinary score")); return; }
    if (LastStage==EMCTutorialStage::FreshSort && StageAge>1.25 && !bWrongFreshChecked)
    {
        auto* Food=Cast<AMCFoodActor>(Director->Players[0].GoalTarget);
        auto* Hero=Cast<AMCToothCharacter>(Director->Players[0].PlayerState->GetPawn());
        if (!Food || !Hero) return;
        for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It)
            if (It->bBrushBin)
            {
                Food->SetStackCarrier(Hero); Food->SetStackCarrier(nullptr);
                Food->SetActorLocation(It->Volume->Bounds.Origin,false,nullptr,ETeleportType::TeleportPhysics);
                Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
                bWrongFreshChecked=true; bWrongFreshPending=true; return;
            }
        Finish(false,TEXT("Tutorial has no front disposal marker")); return;
    }
    if (bWrongFreshPending && LastStage==EMCTutorialStage::FreshSort && StageAge>1.8)
    {
        auto* Food=Cast<AMCFoodActor>(Director->Players[0].GoalTarget);
        if (!Food || Food->IsDisposed() || Director->Players[0].StageProgress!=0)
        { Finish(false,TEXT("The front exit credited or destroyed a fresh tutorial ingredient")); return; }
        bWrongFreshPending=false;
    }
    if (LastStage==EMCTutorialStage::SpoiledSort && StageAge>1.5 && !bWrongSpoiledChecked)
    {
        auto* Food=Cast<AMCFoodActor>(Director->Players[0].GoalTarget);
        auto* Hero=Cast<AMCToothCharacter>(Director->Players[0].PlayerState->GetPawn());
        if (!Food || !Hero) return;
        for (TActorIterator<AMCThroat> It(GetWorld());It;++It)
        {
            const int32 Vomits=It->VomitCount;
            if (Deliver(Food,Hero) || Food->IsDisposed() || It->VomitCount!=Vomits || Director->Players[0].StageProgress!=0)
            { Finish(false,TEXT("Incorrect tutorial throat delivery was swallowed, credited or triggered vomiting")); return; }
            bWrongSpoiledChecked=true; break;
        }
    }
    if (LastStage==EMCTutorialStage::Complete)
    {
        if (!bReadyWaitVerified)
        {
            for (int32 I=0;I+1<Director->Players.Num();++I) Director->SetReady(Director->Players[I].PlayerState,true);
            if (StageAge<3) return;
            if (!GS->bTutorialActive || Director->Stage!=EMCTutorialStage::Complete || FMCTutorialProgressRules::AllReady(Director->Players))
            { Finish(false,TEXT("Training did not wait for the final participant to confirm")); return; }
            bReadyWaitVerified=true;
        }
        if (!bReadySent) { bReadySent=true; Director->SetReady(Director->Players.Last().PlayerState,true); }
        return;
    }
    if (LastStage==EMCTutorialStage::BreakfastCleanup && StageAge>2)
    {
        auto* Hero=Cast<AMCToothCharacter>(Director->Players[0].PlayerState->GetPawn());
        if (!Hero || !Hero->Inventory) return;
        TArray<AMCFoodActor*> Food;
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            if (It->Batch==AMCTutorialDirector::TutorialFoodBatch && !It->bBrushTool && !It->IsDisposed() && It->Phase!=EMCFoodPhase::Swallowing) Food.Add(*It);
        for (auto* Item:Food)
        {
            if (!Item->bFragment)
            {
                Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
                if (Hero->Inventory->Selected==EMCToolSlot::Knife && Hero->Inventory->CanBreak(Item))
                    Item->HitFood(100,FVector::ForwardVector);
            }
            else
            {
                Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
                if (Hero->HasBrush()) Deliver(Item,Hero);
            }
        }
        return;
    }
    if (StageAge<2) return;
    const auto Learners=Director->Players;
    for (int32 I=0;I<Learners.Num();++I)
    {
        const auto& Learner=Learners[I];
        if (I==Learners.Num()-1 && StageAge<3.25) continue;
        if (Learner.StageRequired==0 || Learner.StageProgress>=Learner.StageRequired) continue;
        auto* Hero=Learner.PlayerState?Cast<AMCToothCharacter>(Learner.PlayerState->GetPawn()):nullptr;
        AActor* Target=Learner.GoalTarget; if (!Hero || !IsValid(Target)) continue;
        if (LastStage==EMCTutorialStage::BrushTooth || LastStage==EMCTutorialStage::CoffeeCleanup)
        {
            Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
            if (!Hero->HasBrush()) { Finish(false,TEXT("A cleaning lesson has no usable inventory brush")); return; }
            if (auto* Status=Target->FindComponentByClass<UMCToothStatusComponent>())
                while (Status->NeedsCare(true)) Status->CareContact(true,Hero);
        }
        else if (LastStage==EMCTutorialStage::FoodCut)
        {
            Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
            if (auto* Food=Cast<AMCFoodActor>(Target))
                if (Hero->Inventory->Selected==EMCToolSlot::Knife && Hero->Inventory->CanBreak(Food)
                    && Food->HitFood(100,Hero->GetActorForwardVector()) && Food->IsDisposed()) Director->NotifyAction(Hero,EMCTutorialAction::FoodCut,Food);
        }
        else if (LastStage==EMCTutorialStage::FreshSort)
        {
            Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
            if (Hero->HasBrush()) if (auto* Food=Cast<AMCFoodActor>(Target)) Deliver(Food,Hero);
        }
        else if (LastStage==EMCTutorialStage::SpoiledSort)
        {
            Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
            if (!Hero->HasBrush()) { Finish(false,TEXT("Food sorting has no usable inventory collection tool")); return; }
            auto* Food=Cast<AMCFoodActor>(Target); if (!Food) continue;
            Food->SetStackCarrier(Hero); Food->SetStackCarrier(nullptr);
            for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It)
                if (It->bBrushBin) { Food->SetActorLocation(It->Volume->Bounds.Origin,false,nullptr,ETeleportType::TeleportPhysics); Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector); break; }
        }
        else if (LastStage==EMCTutorialStage::Calculus) ClearCalculus(Director,Hero,Cast<AMCArenaTooth>(Target));
    }
}
