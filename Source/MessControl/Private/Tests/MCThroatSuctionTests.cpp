#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCThroat.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatAmbientField,"MessControl.Throat.AmbientSuctionDistanceAndTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatAmbientField::RunTest(const FString&)
{
    FMCThroatSuctionState State;
    State.Origin=FVector(100,200,150);
    State.StartedAt=20;State.Duration=2;State.InfluenceRadius=6000;State.MaxPullSpeed=85;
    const FVector Near=State.Origin+FVector(-250,0,0),Far=State.Origin+FVector(-4000,0,0);
    TestFalse(TEXT("A future swallow cannot pull before the server event starts"),State.IsActive(19.9));
    TestTrue(TEXT("The onset is an active event with a smooth zero-strength start"),State.IsActive(20) && State.StrengthAt(Near,20)==0);
    const float NearStrength=State.StrengthAt(Near,20.5),FarStrength=State.StrengthAt(Far,20.5);
    TestTrue(TEXT("A player across the room feels the event outside the food delivery zone"),FarStrength>.15f);
    TestTrue(TEXT("Pull becomes noticeably stronger approaching the throat"),NearStrength>FarStrength*2);
    const FVector Drift=State.VelocityAt(Near,20.5);
    TestTrue(TEXT("The draft points toward the inlet, stays horizontal, and is weaker than walking"),Drift.X>0 && FMath::IsNearlyZero(Drift.Y) && FMath::IsNearlyZero(Drift.Z) && Drift.Size()<=85);
    TestTrue(TEXT("Standing directly at the inlet does not produce jitter or lift"),State.VelocityAt(State.Origin+FVector(0,0,-40),20.5).IsNearlyZero());
    TestEqual(TEXT("The atmosphere feathers to zero outside its footprint"),State.StrengthAt(State.Origin+FVector(6100,0,0),20.5),0.f);
    TestTrue(TEXT("A late reader uses the current server phase rather than replaying onset"),State.StrengthAt(Near,20.8)>.8f);
    TestTrue(TEXT("The last part of the gulp fades before it finishes"),State.StrengthAt(Near,21.95)<NearStrength*.15f);
    TestFalse(TEXT("An old event expires even before its stopping update arrives"),State.IsActive(22.1));
    TestTrue(TEXT("Expired suction leaves no residual player drift"),State.VelocityAt(Far,22.1).IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatAmbientLifecycle,"MessControl.Throat.AmbientSuctionFollowsActualMeal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatAmbientLifecycle::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Fixture world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Throat=World->SpawnActor<AMCThroat>(FVector(0,0,2000),FRotator::ZeroRotator);
    auto Step=[&](float Seconds) {for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);}};
    auto Cleanup=[&]() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    if(!TestNotNull(TEXT("Fixture throat"),Throat)) {Cleanup();return false;}
    const FVector Delivery=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter)+FVector(0,0,65);
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
    FMCFoodRow Row;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};Row.Scale=Row.FragmentScale=FVector(.25);
    Row.Mass=1;Row.Fragments=1;Row.SpoilSeconds=300;
    const FTransform FoodTransform(Delivery);
    auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),FoodTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!TestNotNull(TEXT("Fixture ingredient"),Food)) {Cleanup();return false;}
    FRandomStream Random(91);Food->ConfigureItem(TEXT("SuctionFixture"),Row,Random,true);Food->FinishSpawning(FoodTransform);
    Food->Body->SetEnableGravity(false);
    TestTrue(TEXT("The throat reserves a real delivered ingredient"),Throat->AcceptDelivery(Food));
    TestFalse(TEXT("Waiting for the shared gathering deadline creates no suction"),Throat->IsAmbientSuctionActive());
    Step(3.35f);
    TestTrue(TEXT("A real frozen meal starts the replicated atmosphere"),Throat->IsAmbientSuctionActive());
    TestTrue(TEXT("The throat remains relevant to every connected player"),Throat->bAlwaysRelevant);
    TestTrue(TEXT("Server and local queries use the same visible throat inlet"),Throat->GetSuctionOrigin().Equals(Throat->VacuumInlet(),.1f));
    float Strength=0;FVector Velocity;
    TestTrue(TEXT("A distant player receives local atmosphere and horizontal draft"),AMCThroat::FindAmbientSuctionAt(World,Throat->GetSuctionOrigin()+FVector(-3000,0,0),Strength,Velocity) && Strength>.15f && Velocity.X>0 && FMath::IsNearlyZero(Velocity.Z));
    Throat->ResetSwallow();
    TestFalse(TEXT("Cancelling the meal stops the atmosphere immediately"),Throat->IsAmbientSuctionActive());
    TestFalse(TEXT("Reset does not leave stale local atmosphere"),AMCThroat::FindAmbientSuctionAt(World,Delivery,Strength,Velocity));
    TestTrue(TEXT("Reset clears sampled movement and facial strength"),Velocity.IsNearlyZero() && Strength==0);
    TestFalse(TEXT("Reset returns the ingredient alive"),Food->IsDisposed());
    Cleanup();return true;
}
#endif
