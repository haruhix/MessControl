#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCoffeeWipe.h"
#include "MCMouthSurface.h"
#include "MCToothStatusComponent.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCoffeeMaskTest,"MessControl.Liquid.PersistentLocalWipe",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCoffeeMaskTest::RunTest(const FString&)
{
    TArray<uint8> Mask; FMCCoffeeWipe::Reset(Mask);
    TestTrue(TEXT("Valid stroke removes local liquid"),FMCCoffeeWipe::Stroke(Mask,{.25f,.25f},{.5f,.25f},.12f,.1f));
    TestTrue(TEXT("Bristle path cleared"),Mask[(FMCCoffeeWipe::Size/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/8]<255);
    TestEqual(TEXT("Opposite side remains wet"),Mask[(FMCCoffeeWipe::Size*3/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/4],uint8(255));
    const auto Before=Mask;
    TestFalse(TEXT("Distant stroke ignored"),FMCCoffeeWipe::Stroke(Mask,{-4,-4},{-3,-3},.1f,.1f));
    TestFalse(TEXT("NaN time ignored"),FMCCoffeeWipe::Stroke(Mask,{.5f,.5f},{.6f,.5f},.1f,std::numeric_limits<float>::quiet_NaN()));
    TestFalse(TEXT("Negative time ignored"),FMCCoffeeWipe::Stroke(Mask,{.5f,.5f},{.6f,.5f},.1f,-1));
    TestTrue(TEXT("Invalid input preserves previous tracks"),Mask==Before);
    FMCCoffeeWipe::Stroke(Mask,{.7f,.6f},{.7f,.8f},.12f,.1f);
    TestEqual(TEXT("Second worker cannot refill first track"),Mask[(FMCCoffeeWipe::Size/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/8],Before[(FMCCoffeeWipe::Size/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size*3/8]);
    auto Clamped=Before,Huge=Before;
    FMCCoffeeWipe::Stroke(Clamped,{.5f,.5f},{.6f,.5f},.12f,.1f);
    FMCCoffeeWipe::Stroke(Huge,{.5f,.5f},{.6f,.5f},.12f,1000);
    TestTrue(TEXT("Large time step is bounded"),Huge==Clamped);
    FMCCoffeeWipe::Reset(Mask); TestEqual(TEXT("Fresh spill restores liquid"),FMCCoffeeWipe::Remaining(Mask),1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCoffeeContactTest,"MessControl.Liquid.AuthoritativeBrushContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCoffeeContactTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));
    World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
    World->GetAuthGameMode<AMCGameMode>()->bUseDayOnePlan=false;
    auto* GS=World->GetGameState<AMCGameState>(); GS->Phase=EMCShiftPhase::Working; GS->bPhysicalBrushes=false;
    auto* Patch=World->SpawnActor<AMCMouthSurface>(FVector(300,0,5),FRotator::ZeroRotator);
    Patch->LiquidHalfSize=92; Patch->Status->ApplyCoffee();
    auto* A=World->SpawnActor<AMCToothCharacter>(FVector(235,0,80),FRotator::ZeroRotator);
    auto* B=World->SpawnActor<AMCToothCharacter>(FVector(365,0,80),FRotator(0,180,0));
    for (auto* Worker:{A,B}) { Worker->SetActorTickEnabled(false); Worker->GetCharacterMovement()->DisableMovement(); Worker->bBrushing=true; }
    A->AdvanceCare(.1f);
    TestEqual(TEXT("Approaching brush cannot erase liquid immediately"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    // Brush acquisition uses server time, including on a server without rendering.
    for (int32 I=0;I<8;++I) { ++GFrameCounter; World->Tick(LEVELTICK_TimeOnly,.1f); A->AdvanceCare(.1f); }
    TestTrue(TEXT("Held brush makes a persistent local trail"),FMCCoffeeWipe::Remaining(Patch->WipeMask)<.99f);
    TestTrue(TEXT("A local trail leaves the rest of the stain dirty"),Patch->Status->State.CoffeeLeft>0);
    const auto Mask=Patch->WipeMask;
    A->SetActorLocation(FVector(2000,0,80)); A->AdvanceCare(.1f);
    TestTrue(TEXT("Out of reach preserves trail"),Mask==Patch->WipeMask);
    A->SetActorLocation(FVector(235,0,80)); Patch->Status->ApplyCoffee(); A->ResetContact(); B->ResetContact();
    TestEqual(TEXT("Reapplying same amount resets old trail"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    for (int32 I=0;I<240 && !Patch->IsClean();++I) { ++GFrameCounter; World->Tick(LEVELTICK_TimeOnly,.1f); A->AdvanceCare(.1f); B->AdvanceCare(.1f); }
    TestTrue(TEXT("Two stationary workers seek and clean the remaining visible dirt"),Patch->IsClean());
    TestTrue(TEXT("Completion follows actual visible coverage"),Patch->RemainingLiquid()<.025f);
    Patch->Status->ApplyCoffee(); GS->bPhysicalBrushes=true; A->Inventory->ServerSelect(EMCToolSlot::Pickaxe); A->bBrushing=true;
    Patch->BrushLiquid(A,.1f);
    TestEqual(TEXT("A non-cleaning slot cannot erase liquid"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    A->Inventory->ServerSelect(EMCToolSlot::Brush);
    TestTrue(TEXT("Returning to slot one restores the brush without a physical pickup"),A->HasBrush());
    A->bBrushing=true; Patch->bUlcer=true; Patch->BrushLiquid(A,.1f);
    TestEqual(TEXT("Ulcers are never wiped by the liquid path"),FMCCoffeeWipe::Remaining(Patch->WipeMask),1.f);
    World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
