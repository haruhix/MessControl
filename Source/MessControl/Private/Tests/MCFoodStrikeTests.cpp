#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FFoodStrikeWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    FFoodStrikeWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
        Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
    }
    ~FFoodStrikeWorld()
    {
        World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void Step(float Seconds,float Dt=1.f/60.f)
    {
        for (int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I)
        { ++GFrameCounter; World->Tick(LEVELTICK_All,Dt); }
    }
    AMCFoodActor* Food(const FMCFoodRow& Row,FVector Location)
    {
        const FTransform Transform(FRotator::ZeroRotator,Location);
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(7); Food->ConfigureItem(TEXT("KnifeFixture"),Row,Random);
        Food->FinishSpawning(Transform); Food->Body->SetSimulatePhysics(false); Food->SetActorTickEnabled(false);
        return Food;
    }
};
UStaticMesh* StrikeMesh(const TCHAR* Path)
{
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,Path);
#if WITH_EDITOR
    if (Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
    return Mesh;
}
FMCFoodRow CubeRow()
{
    FMCFoodRow Row; Row.WholeMeshes={StrikeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"))};
    Row.FragmentMeshes=Row.WholeMeshes; Row.Scale=Row.FragmentScale=FVector(.5); Row.Health=100; Row.Resistance=EMCFoodResistance::Soft;
    return Row;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCGrapeKnifeContact,"MessControl.Inventory.Knife.GrapeSurfaceHits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCGrapeKnifeContact::RunTest(const FString&)
{
    // Food is frozen so the geometry fixtures stay at their measured contacts.
    AddExpectedError(TEXT("has to have 'Simulate Physics' enabled if you'd like to AddImpulse"),EAutomationExpectedErrorFlags::Contains,0);
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if (!TestNotNull(TEXT("Saved breakfast menu"),Table)) return false;
    const auto* Menu=Table->FindRow<FMCFoodRow>(TEXT("Bacon"),TEXT("Grape knife regression"));
    if (!TestNotNull(TEXT("Current grape row"),Menu)) return false;
    int32 OldRejected=0,InsideBounds=0;
    for (const TCHAR* Path:{TEXT("/Game/Stylized_Fruits/Meshes/SM_GrapesGreen.SM_GrapesGreen"),TEXT("/Game/Stylized_Fruits/Meshes/SM_GrapesPurple.SM_GrapesPurple")})
    {
        auto* Mesh=StrikeMesh(Path); if (!TestNotNull(TEXT("Real grape mesh"),Mesh)) return false;
        for (int32 Side=0;Side<8;++Side) for (float StandOff:{40.f,130.f})
        {
            FFoodStrikeWorld T; FMCFoodRow Row=*Menu; Row.WholeMeshes={Mesh};
            auto* Food=T.Food(Row,FVector(0,0,2000));
            const FVector Direction=FRotator(0,Side*45.f,0).Vector();
            FHitResult Surface;
            const FVector From=Food->Visual->Bounds.Origin-Direction*(Food->Visual->Bounds.SphereRadius+300);
            if (!TestTrue(TEXT("Fixture reaches the actual grape surface"),Food->FindGripSurface(From,Surface))) continue;
            const FVector HeroPosition=Surface.ImpactPoint-Direction*StandOff;
            T.Hero->SetActorLocationAndRotation(HeroPosition,Direction.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
            const FVector OldOffset=Food->Visual->Bounds.GetBox().GetClosestPointTo(HeroPosition)-HeroPosition;
            InsideBounds+=Food->Visual->Bounds.GetBox().IsInside(HeroPosition);
            OldRejected+=!T.Hero->CanContact(Food) || FVector::DotProduct(Direction,OldOffset.GetSafeNormal2D())<=.25f;
            T.Hero->SwingBrush(); T.Step(.5f);
            TestEqual(*FString::Printf(TEXT("%s side %d registers one visible knife strike"),*Mesh->GetName(),Side),T.Hero->ConfirmedHitCount,1);
            TestEqual(TEXT("Contact retries cannot multiply damage"),Food->Health,Row.Health-T.Hero->Inventory->Damage());
        }
    }
    TestTrue(TEXT("Real mesh fixtures reproduce the old contact rejection"),OldRejected>0);
    TestTrue(TEXT("Contacts include a player inside the grape bounding box"),InsideBounds>0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnifeMovingContact,"MessControl.Inventory.Knife.MovingFoodAndSingleDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnifeMovingContact::RunTest(const FString&)
{
    AddExpectedError(TEXT("has to have 'Simulate Physics' enabled if you'd like to AddImpulse"),EAutomationExpectedErrorFlags::Contains,0);
    for (float Dt:{1.f/30.f,1.f/60.f,1.f/120.f})
    {
        FFoodStrikeWorld T; auto* Food=T.Food(CubeRow(),T.Hero->GetActorLocation()+FVector(500,0,0));
        T.Hero->SwingBrush(); T.Step(.30f,Dt);
        TestEqual(TEXT("Distant food misses the first contact sample"),T.Hero->ConfirmedHitCount,0);
        Food->SetActorLocation(T.Hero->GetActorLocation()+FVector(130,0,0),false,nullptr,ETeleportType::TeleportPhysics);
        T.Step(.2f,Dt);
        TestEqual(TEXT("Food entering the follow-through registers"),T.Hero->ConfirmedHitCount,1);
        TestEqual(TEXT("One swing applies exactly one damage amount"),Food->Health,100-T.Hero->Inventory->Damage());
        for (int32 I=0;I<10;++I) T.Hero->SwingBrush();
        T.Step(.1f,Dt);
        TestEqual(TEXT("Input spam during the cooldown does not create more hits"),T.Hero->ConfirmedHitCount,1);
        T.Step(.2f,Dt); T.Hero->SwingBrush(); T.Step(.5f,Dt);
        TestEqual(TEXT("A later swing can hit the same food again"),T.Hero->ConfirmedHitCount,2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnifeContactProtection,"MessControl.Inventory.Knife.ReachOcclusionAndCut",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnifeContactProtection::RunTest(const FString&)
{
    AddExpectedError(TEXT("has to have 'Simulate Physics' enabled if you'd like to AddImpulse"),EAutomationExpectedErrorFlags::Contains,0);
    FFoodStrikeWorld T; auto* Food=T.Food(CubeRow(),T.Hero->GetActorLocation()+FVector(130,0,0));
    auto* Wall=T.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(5,300,300)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
    Wall->SetActorLocation(T.Hero->GetActorLocation()+FVector(60,0,0));
    T.Hero->SwingBrush(); T.Step(.8f); TestEqual(TEXT("A wall blocks the complete knife contact window"),Food->Health,100.f);
    Wall->Destroy();
    auto* PartialWall=T.World->SpawnActor<AActor>(); auto* PartialBox=NewObject<UBoxComponent>(PartialWall);
    PartialWall->SetRootComponent(PartialBox); PartialBox->SetBoxExtent(FVector(5,300,12)); PartialBox->SetCollisionProfileName(TEXT("BlockAll")); PartialBox->RegisterComponent();
    PartialWall->SetActorLocation(T.Hero->GetActorLocation()+FVector(60,0,12));
    T.Hero->SwingBrush(); T.Step(.8f);
    TestEqual(TEXT("An exposed food surface remains hittable when another contact height is blocked"),Food->Health,100-T.Hero->Inventory->Damage());
    PartialWall->Destroy(); Food->Health=100; const int32 HitsBeforeCut=T.Hero->ConfirmedHitCount;
    Food->SetActorLocation(T.Hero->GetActorLocation()-FVector(130,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    T.Hero->SwingBrush(); T.Step(.8f); TestEqual(TEXT("Food behind the player cannot be cut"),Food->Health,100.f);
    Food->SetActorLocation(T.Hero->GetActorLocation()+FVector(400,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    T.Hero->SwingBrush(); T.Step(.8f); TestEqual(TEXT("Food outside weapon reach cannot be cut"),Food->Health,100.f);
    Food->SetActorLocation(T.Hero->GetActorLocation()+FVector(130,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    Food->FoodData.Resistance=EMCFoodResistance::Hard;
    T.Hero->SwingBrush(); T.Step(.8f); TestEqual(TEXT("The knife still rejects hard food"),Food->Health,100.f);
    Food->FoodData.Resistance=EMCFoodResistance::Soft;
    for (int32 I=0;I<4;++I) { T.Hero->SwingBrush(); T.Step(.8f); }
    TestTrue(TEXT("Four valid strikes cut the whole food"),Food->IsDisposed());
    TestEqual(TEXT("Four valid swings register four damage events"),T.Hero->ConfirmedHitCount-HitsBeforeCut,4);
    int32 Fragments=0; for (TActorIterator<AMCFoodActor> It(T.World);It;++It) Fragments+=It->bFragment;
    TestEqual(TEXT("Cutting preserves the configured fragment count"),Fragments,Food->FoodData.Fragments);
    return true;
}
#endif
