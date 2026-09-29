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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSprayProtection,"MessControl.Inventory.SprayCooldownAndNaturalHealing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSprayProtection::RunTest(const FString&) {
    FInventoryWorld T; auto* I=T.H->Inventory.Get(); I->ServerSelect(EMCToolSlot::Spray);
    I->ServerSpray(); TestEqual(TEXT("Missing target does not consume cooldown"),I->SprayReadyAt,0.);
    auto* Patch=T.W->SpawnActor<AMCMouthSurface>(FVector(-470,0,25),FRotator::ZeroRotator); Patch->bUlcer=true;
    I->ServerSpray(); TestTrue(TEXT("Spray numbs a nearby forward ulcer"),Patch->IsNumb()); TestTrue(TEXT("Cooldown starts on a successful use"),I->SpraySecondsLeft()>7);
    const double Ready=I->SprayReadyAt,Until=Patch->NumbUntil;
    I->ServerSpray(); TestEqual(TEXT("Holding fire cannot extend freeze during cooldown"),Patch->NumbUntil,Until); TestEqual(TEXT("Cooldown is stable"),I->SprayReadyAt,Ready);
    const float HP=T.GS->MouthHealth; Patch->Healing=.25f; Patch->Disturb(); Patch->Tick(.5f);
    TestTrue(TEXT("Numb ulcer continues natural healing"),Patch->Healing>.25f); TestEqual(TEXT("Numb ulcer does no mouth damage"),T.GS->MouthHealth,HP);
    Patch->NumbUntil=-1; Patch->Tick(.1f); TestTrue(TEXT("Damage returns after protection expires"),T.GS->MouthHealth<HP);
    I->SprayReadyAt=0; Patch->SetActorLocation(FVector(-1700,0,25)); I->ServerSpray(); TestEqual(TEXT("Distant ulcer rejected"),I->SprayReadyAt,0.);
    Patch->SetActorLocation(FVector(-720,0,25)); I->ServerSpray(); TestEqual(TEXT("Ulcer behind player rejected"),I->SprayReadyAt,0.);
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
