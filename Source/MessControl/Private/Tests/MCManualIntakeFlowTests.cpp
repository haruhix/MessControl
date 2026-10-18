#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCThroat.h"
#include "MCThroatVortex.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCManualIntakeFlowTest,"MessControl.Throat.ManualIntakeIndividualFlow",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCManualIntakeFlowTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Throat=World->SpawnActor<AMCThroat>(FVector(10000,0,2000),FRotator::ZeroRotator);
    Throat->bManualIntake=true;Throat->GateCenter=FVector::ZeroVector;
    const FVector Inlet=Throat->GetActorLocation()+FVector(0,0,230);
    auto* Worker=World->SpawnActor<AMCToothCharacter>(FVector(8000,0,3000),FRotator::ZeroRotator);
    auto FoodAt=[&](FVector Position) {
        auto* Food=World->SpawnActor<AMCFoodActor>(Position,FRotator::ZeroRotator);
        Food->Phase=EMCFoodPhase::Free;Food->Body->SetSimulatePhysics(false);
        Food->MarkManualThrow(Worker);
        return Food;
    };
    auto* First=FoodAt(Inlet+FVector(30,-35,0));
    auto* Second=FoodAt(Inlet+FVector(35,35,0));
    auto* Miss=FoodAt(Inlet+FVector(30,1000,0));
    auto* Unthrown=World->SpawnActor<AMCFoodActor>(Inlet+FVector(30,0,0),FRotator::ZeroRotator);
    TestFalse(TEXT("Fresh loose food is never automatically delivered"),Throat->AcceptDelivery(Unthrown));
    TestFalse(TEXT("Public delivery rejects food outside the aperture"),Throat->AcceptDelivery(Miss));
    TestTrue(TEXT("First loose food enters intake"),Throat->AcceptDelivery(First));
    TWeakObjectPtr<AMCThroatVortex> SharedVortex;
    for(TActorIterator<AMCThroatVortex> It(World);It;++It) if(!It->IsActorBeingDestroyed()) SharedVortex=*It;
    TestTrue(TEXT("First intake creates its shared effect"),SharedVortex.IsValid());
    // A dedicated server has no presentation tick. Joining food must extend
    // that same actor rather than replaying the onset on connected clients.
    if(SharedVortex.IsValid()) SharedVortex->SetActorTickEnabled(false);
    TestTrue(TEXT("Second food joins without replacing the first"),Throat->AcceptDelivery(Second));
    TestTrue(TEXT("Disabled presentation tick does not expire a live vortex"),SharedVortex.IsValid() && !SharedVortex->IsActorBeingDestroyed());
    if(SharedVortex.IsValid()) SharedVortex->SetActorTickEnabled(true);
    TestFalse(TEXT("Food remains visible while pulling"),First->IsDisposed());
    TestEqual(TEXT("Both pending foods are tracked"),Throat->QueuedFoodCount,2);
    TestEqual(TEXT("No score before reaching the throat"),Throat->FoodSwallowed,0);
    int32 Vortices=0;for(TActorIterator<AMCThroatVortex> It(World);It;++It) if(!It->IsActorBeingDestroyed()) ++Vortices;
    TestEqual(TEXT("Food shares one bounded vortex"),Vortices,1);
    const FVector Initial=First->GetActorLocation();
    for(int32 I=0;I<8;++I) {++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}
    TestTrue(TEXT("Intake moves the real food"),FVector::DistSquared(Initial,First->GetActorLocation())>100.f);
    TestTrue(TEXT("Foods retain distinct paths"),FVector::DistSquared(First->GetActorLocation(),Second->GetActorLocation())>1.f);
    TestFalse(TEXT("Pull is not an immediate disappearance"),First->IsDisposed());
    for(int32 I=0;I<30;++I) {++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}
    TestTrue(TEXT("First food resolves after visible travel"),First->IsDisposed());
    TestTrue(TEXT("Second food resolves exactly once"),Second->IsDisposed());
    TestEqual(TEXT("Two successful deliveries"),Throat->FoodSwallowed,2);
    auto* Cancelled=FoodAt(Inlet+FVector(30,0,0));
    const FVector CancelledStart=Cancelled->GetActorLocation();
    TestTrue(TEXT("Third intake begins"),Throat->AcceptDelivery(Cancelled));
    ++GFrameCounter;World->Tick(LEVELTICK_All,.1f);
    Throat->ResetSwallow();
    TestEqual(TEXT("Run reset releases reserved food"),Cancelled->Phase,EMCFoodPhase::Free);
    TestFalse(TEXT("Reset never credits or destroys pending food"),Cancelled->IsDisposed());
    TestEqual(TEXT("Reset clears queue"),Throat->QueuedFoodCount,0);
    TestTrue(TEXT("Reset returns pending food to its initial position"),Cancelled->GetActorLocation().Equals(CancelledStart,.01f));
    TestFalse(TEXT("Reset requires a new manual throw before another intake"),Cancelled->WasManuallyThrown());
    ++GFrameCounter;World->Tick(LEVELTICK_All,.1f);
    TestEqual(TEXT("A normal tick cannot recapture reset food"),Throat->QueuedFoodCount,0);
    TestFalse(TEXT("Reset food remains uncredited and undisposed"),Cancelled->IsDisposed());
    auto* LateExtension=World->SpawnActor<AMCThroatVortex>(FVector(15000,0,2000),FRotator::ZeroRotator);
    LateExtension->StartedAt=World->GetTimeSeconds()-10.;LateExtension->Duration=1.f;
    LateExtension->Tick(.016f);
    TestFalse(TEXT("Expired local presentation is hidden"),LateExtension->Airflow->IsVisible());
    TestTrue(TEXT("Expired presentation retains its cheap extension check"),LateExtension->IsActorTickEnabled());
    LateExtension->Duration=20.f;LateExtension->Tick(.016f);
    TestTrue(TEXT("A late duration extension restores presentation"),LateExtension->Airflow->IsVisible());
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
