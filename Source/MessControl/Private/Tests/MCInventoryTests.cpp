#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCThroat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraSystem.h"
#include "MCBrushContactComponent.h"

namespace {
struct FInventoryWorld {
    UWorld* W; AMCGameState* GS; AMCToothCharacter* H;
    FInventoryWorld() {
        W=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        W->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
        W->SetGameMode(URL); W->InitializeActorsForPlay(URL); W->BeginPlay();
        GS=W->GetGameState<AMCGameState>(); GS->Phase=EMCShiftPhase::Working; GS->bDevManualEvents=true;
        for(TActorIterator<AMCThroat> It(W);It;++It) { It->SetActorEnableCollision(false); It->SetActorTickEnabled(false); }
        H=W->SpawnActor<AMCToothCharacter>(FVector(-600,0,98),FRotator::ZeroRotator);
        H->GetCharacterMovement()->DisableMovement();
    }
    ~FInventoryWorld() { W->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(W); W->DestroyWorld(false); }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCInventoryRouting,"MessControl.Inventory.SelectionAndMaterial",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCInventoryRouting::RunTest(const FString&) {
    FInventoryWorld T; auto* I=T.H->Inventory.Get();
    auto* Hard=T.W->SpawnActor<AMCFoodActor>(FVector(-480,0,60),FRotator::ZeroRotator); Hard->FoodData.Resistance=EMCFoodResistance::Hard;
    auto* Soft=T.W->SpawnActor<AMCFoodActor>(FVector(-480,150,60),FRotator::ZeroRotator); Soft->FoodData.Resistance=EMCFoodResistance::Soft;
    TestTrue(TEXT("Default slot is the cleaning tool"),T.H->HasBrush());
    T.H->bBrushing=true; I->ServerSelect(EMCToolSlot::Pickaxe);
    TestFalse(TEXT("Changing slot cancels the cleaning contact"),T.H->bBrushing || T.H->HasBrush());
    TestTrue(TEXT("Pickaxe accepts hard food"),I->CanBreak(Hard)); TestFalse(TEXT("Pickaxe rejects soft food"),I->CanBreak(Soft));
    I->ServerSelect(EMCToolSlot::Knife); TestTrue(TEXT("Knife accepts soft food"),I->CanBreak(Soft)); TestFalse(TEXT("Knife rejects hard food"),I->CanBreak(Hard));
    I->ServerSelect(static_cast<EMCToolSlot>(99)); TestEqual(TEXT("Invalid slot ignored"),I->Selected,EMCToolSlot::Knife);
    T.H->SwingBrush(); I->ServerSelect(EMCToolSlot::Spray); TestEqual(TEXT("Cannot swap the damage tool during a swing"),I->Selected,EMCToolSlot::Knife);
    TestTrue(TEXT("Pickaxe lifts overhead then swings down"),UMCInventoryComponent::SwingAngle(EMCToolSlot::Pickaxe,.30f)>100 && UMCInventoryComponent::SwingAngle(EMCToolSlot::Pickaxe,.44f)<-100);
    I->UnlockWaterJet(); TestTrue(TEXT("Upgrade is retained in slot one"),I->bWaterJetUnlocked);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSprayProtection,"MessControl.Inventory.HoldSprayAndPreserveProgress",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSprayProtection::RunTest(const FString&) {
    FInventoryWorld T; auto* I=T.H->Inventory.Get(); I->ServerSelect(EMCToolSlot::Spray);
    auto* Patch=T.W->SpawnActor<AMCMouthSurface>(FVector(-470,0,25),FRotator::ZeroRotator); Patch->bUlcer=true;
    Patch->HealSeconds=7;
    auto Tick=[&](int32 Count) { for(int32 N=0;N<Count;++N) { ++GFrameCounter; I->TickComponent(.1f,LEVELTICK_All,nullptr); } };
    T.H->ServerSetPrimary(true); Tick(20);
    TestTrue(TEXT("Two seconds of held spray heals two sevenths"),FMath::IsNearlyEqual(Patch->Healing,2.f/7,.0001f));
    TestTrue(TEXT("Treatment numbs the ulcer"),Patch->IsNumb());
    const float Saved=Patch->Healing,HP=T.GS->MouthHealth;
    for(int32 N=0;N<30;++N) I->ServerSpray();
    TestEqual(TEXT("Repeated requests cannot manufacture treatment time"),Patch->Healing,Saved);
    I->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Duplicate ticks in one frame cannot accelerate treatment"),Patch->Healing,Saved);
    Patch->Disturb(); Patch->Tick(.5f); TestEqual(TEXT("Protected lesion does no mouth damage"),T.GS->MouthHealth,HP);
    T.H->ServerSetPrimary(false); Tick(10); Patch->NumbUntil=-1; Patch->Disturb(); Patch->Tick(.5f);
    TestEqual(TEXT("Release, contact and idle time preserve progress"),Patch->Healing,Saved);
    TestTrue(TEXT("Damage returns while untreated"),T.GS->MouthHealth<HP);
    T.H->ServerSetPrimary(true); Patch->SetActorLocation(FVector(-1700,0,25)); Tick(10);
    TestEqual(TEXT("Distant ulcer cannot be treated"),Patch->Healing,Saved);
    Patch->SetActorLocation(FVector(-720,0,25)); Tick(10);
    TestEqual(TEXT("Ulcer behind player cannot be treated"),Patch->Healing,Saved);
    Patch->SetActorLocation(FVector(-470,0,25)); Tick(49);
    TestFalse(TEXT("Less than seven seconds is incomplete"),Patch->IsHealed());
    Tick(1); TestTrue(TEXT("Resumed treatment completes after seven effective seconds"),Patch->IsHealed());
    Tick(10); TestEqual(TEXT("Completed treatment stays at one"),Patch->Healing,1.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoamAsset,"MessControl.Inventory.NiagaraAsset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoamAsset::RunTest(const FString&) {
    const auto* Default=GetDefault<UMCBrushContactComponent>();
    const auto* System=Default->FoamSystem.LoadSynchronous();
    if(!TestNotNull(TEXT("Authored Niagara foam ships with the project"),System)) return false;
    TestTrue(TEXT("Foam contains an emitter"),System->GetNumEmitters()>0);
    return true;
}
#endif
