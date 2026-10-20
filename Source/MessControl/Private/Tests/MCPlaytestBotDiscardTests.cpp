#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCFoodDirectorHooks.h"
#include "MCMouthSurface.h"
#include "MCPlaytestBotController.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCPlaytestBotDiscardTestsPrivate
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    }
    ~FFixture() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step() {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestActualFrontDiscard,
    "MessControl.PlaytestBots.Adaptation.SpoiledFragmentFliesBeyondActualFront",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestActualFrontDiscard::RunTest(const FString&)
{
    MCPlaytestBotDiscardTestsPrivate::FFixture F;
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("Native tongue source mesh exists"),Plane) || !TestNotNull(TEXT("Physical fragment mesh exists"),Cube)) return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({Plane,Cube});
#endif
    // Translation and a deliberately obsolete, interior marker make the old
    // Volume + extent aim miss the actual lip. No director or stage shortcuts.
    const FTransform Pose(FQuat::Identity,FVector(10000,8000,1000),FVector(30,20,1));
    auto* Tongue=F.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose);
    Tongue->SourceMesh=Plane;Tongue->Settings.IdleHeight=0;Tongue->bAutomaticYawns=false;
    Tongue->FinishSpawning(Pose);Tongue->SetActorTickEnabled(false);
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    auto* Exit=F.World->SpawnActor<AMCFoodDisposal>(Bounds.GetCenter()-FVector(200,0,0),FRotator::ZeroRotator);
    Exit->bBrushBin=true;Exit->bEnableLegacyDeliveryZone=false;
    Exit->Volume->SetBoxExtent(FVector(350,1000,220));
    TArray<FVector> Outer,Inner;
    if(!TestTrue(TEXT("Retired marker still supplies the real tongue silhouette"),Exit->GetDeliveryZoneOutline(Outer,Inner) && Outer.Num()>1)) return false;
    const FVector Lip=Outer[Outer.Num()/2];
    // The real front teeth extend beyond the tongue lip and above its low
    // edge. A target inside that border blocks both arcs, even the high one.
    auto* FrontBorder=F.World->SpawnActor<AActor>();
    auto* Border=NewObject<UBoxComponent>(FrontBorder);
    FrontBorder->SetRootComponent(Border);Border->SetBoxExtent(FVector(350,1200,150));
    Border->SetCollisionProfileName(TEXT("BlockAll"));Border->RegisterComponent();
    FrontBorder->SetActorLocation(FVector(Lip.X-100,Bounds.GetCenter().Y,Bounds.Max.Z+150));
    auto* Hero=F.World->SpawnActor<AMCToothCharacter>(FVector(Lip.X+600,Bounds.GetCenter().Y,Bounds.Max.Z+120),FRotator(0,180,0));
    Hero->SetActorLocation(FVector(Lip.X+600,Bounds.GetCenter().Y,Bounds.Max.Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4.f));
    Hero->GetCharacterMovement()->DisableMovement(); // Already at an exposed, valid throw stance; navigation is a separate PIE check.
    auto* Bot=F.World->SpawnActor<AMCPlaytestBotController>();
    Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);Bot->Possess(Hero);
    FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};
    Row.Scale=Row.FragmentScale=FVector(.2);Row.Collision.bAutoOptimize=false;Row.SpoilSeconds=300;
    const FTransform FoodPose(Hero->GetActorLocation()+FVector(-70,0,-20));
    auto* Food=F.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),FoodPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    FRandomStream Random(41);Food->ConfigureItem(TEXT("BotSpoiledFrontFixture"),Row,Random,true);
    Food->Batch=74141;Food->bSpoiled=true;Food->FinishSpawning(FoodPose);
    Hero->SetFoodCollectionInput(true);
    if(!TestTrue(TEXT("The initial stack passes ordinary pickup validation"),Hero->FoodCollection->Collect(Food))) return false;
    auto* Dirt=F.World->SpawnActor<AMCMouthSurface>(Bounds.GetCenter()+FVector(0,200,5),FRotator::ZeroRotator);
    Dirt->Batch=Food->Batch;Dirt->Tags.Add(TEXT("MCFoodServedDirt"));Dirt->SetOwner(Food);Dirt->Status->ApplyCoffee();
    TestEqual(TEXT("The spoiled fragment initially remains in its real meal pipeline"),MCMeasureFoodPipeline(F.World,Food->Batch).OutstandingActors,1);
    TestFalse(TEXT("Fixture setup has not marked a throw"),Food->WasManuallyThrown());
    bool SawThrow=false,SawFreeFlight=false,ClearedLip=false,Disposed=false;
    float Highest=float(Food->GetActorLocation().Z);
    const float InitialHeight=Highest;
    // Only ordinary controller timers/Q and Chaos advance the piece after this
    // point: no target relocation, simulated damage, dispose call or time jump.
    for(int32 Frame=0;Frame<480 && !Disposed;++Frame) {
        F.Step();
        SawThrow|=Food->WasManuallyThrown();
        SawFreeFlight|=Food->WasManuallyThrown() && !Food->StackCarrier && Food->Body->IsSimulatingPhysics();
        Highest=FMath::Max(Highest,float(Food->GetActorLocation().Z));
        ClearedLip|=Food->Body->Bounds.GetBox().Max.X<Lip.X;
        Disposed=Food->IsDisposed();
    }
    TestTrue(TEXT("The real bot issues a manual Q throw"),SawThrow);
    TestTrue(TEXT("The released fragment has actual free rigid-body flight"),SawFreeFlight);
    TestTrue(TEXT("The arc rises above its launch instead of targeting the floor"),Highest>InitialHeight+20.f);
    TestTrue(TEXT("The complete fragment physically crosses the actual lip"),ClearedLip);
    TestTrue(TEXT("Native front-discard validation completes the spoiled ingredient"),Disposed);
    TestEqual(TEXT("Actual discard removes the outstanding food from its batch"),MCMeasureFoodPipeline(F.World,Food->Batch).OutstandingActors,0);
    TestTrue(TEXT("Throwing away spoiled food retains the independent cleaning task"),MCCountDirectedFoodDirt(F.World,Food->Batch)>0);
    return true;
}
#endif
