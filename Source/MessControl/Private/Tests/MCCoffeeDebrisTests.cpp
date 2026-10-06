#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCoffeeFlood.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
    struct FRiverDebrisWorld
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        AMCCoffeeFlood* Flood=nullptr;
        UStaticMesh* Cube=nullptr;
        UMCDayPlan* Plan=nullptr;
        FRiverDebrisWorld()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
            auto* State=World->SpawnActor<AMCGameState>(); State->Phase=EMCShiftPhase::Working; World->SetGameState(State);
            Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
            auto* Floor=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(6000,6000,10)); Box->SetCollisionProfileName(TEXT("BlockAll"));
            Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
            auto* Outlet=World->SpawnActor<AMCFoodDisposal>(FVector(1900,0,0),FRotator::ZeroRotator);
            Outlet->SetActorEnableCollision(false); Outlet->SetActorTickEnabled(false);
            Plan=NewObject<UMCDayPlan>(); Plan->ArenaHalfSize=FVector(2000,1500,220); Plan->CoffeeProfile.Reset();
            Flood=World->SpawnActor<AMCCoffeeFlood>(); Flood->Start(Plan);
            Flood->WaterSettings.Cycles=1; Flood->Seconds=Flood->WaterSettings.CycleSeconds();
        }
        ~FRiverDebrisWorld()
        {
            World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
        void Step(float Seconds)
        {
            for (int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) {++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60);}
        }
        void PutBodyInWave(FVector P)
        {
            const float Along=FVector::DotProduct(P-Flood->RiverOrigin,Flood->RiverDirection);
            Flood->StartedAt=World->GetTimeSeconds()-(.35f+(Along+Flood->RiverWidth*.35f)/Flood->RiverSpeed);
        }
        AMCFoodActor* Food(FVector P,FVector Scale=FVector(.4),float Mass=4,bool Fragment=true)
        {
            const FTransform Pose(P);
            auto* F=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            FMCFoodRow Row; Row.WholeMeshes={Cube}; Row.FragmentMeshes={Cube}; Row.Scale=Row.FragmentScale=Scale;
            Row.Mass=Mass; Row.Fragments=2; Row.SpoilSeconds=300;
            FRandomStream Random(41); F->ConfigureItem(TEXT("RiverDebrisFixture"),Row,Random,Fragment); F->FinishSpawning(Pose);
            return F;
        }
        UStaticMeshComponent* Prop(FVector P,float HalfExtent=20,float Mass=2,bool Simulate=true)
        {
            auto* Actor=World->SpawnActor<AActor>(); auto* Body=NewObject<UStaticMeshComponent>(Actor);
            Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body); Body->SetMobility(EComponentMobility::Movable);
            Body->SetStaticMesh(Cube); Body->SetCollisionProfileName(TEXT("PhysicsActor")); Body->SetGenerateOverlapEvents(false);
            Body->RegisterComponent(); Actor->SetActorScale3D(FVector(HalfExtent/50)); Actor->SetActorLocation(P);
            Body->SetMassOverrideInKg(NAME_None,Mass,true); Body->SetLinearDamping(.7f); Body->SetSimulatePhysics(Simulate);
            return Body;
        }
        UProceduralMeshComponent* ProceduralPiece(FVector P)
        {
            auto* Actor=World->SpawnActor<AActor>(); auto* Body=NewObject<UProceduralMeshComponent>(Actor);
            Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body); Body->SetMobility(EComponentMobility::Movable);
            Body->SetCollisionProfileName(TEXT("PhysicsActor")); Body->bUseComplexAsSimpleCollision=false; Body->bUseAsyncCooking=false;
            Body->RegisterComponent();
            const TArray<FVector> Vertices={FVector(-20,-20,-20),FVector(20,-20,-20),FVector(20,20,-20),FVector(-20,20,-20),
                FVector(-20,-20,20),FVector(20,-20,20),FVector(20,20,20),FVector(-20,20,20)};
            const TArray<int32> Indices={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7};
            Body->CreateMeshSection(0,Vertices,Indices,{},{},{},{},true); Body->AddCollisionConvexMesh(Vertices);
            Actor->SetActorLocation(P); Body->SetMassOverrideInKg(NAME_None,2,true); Body->SetLinearDamping(.7f); Body->SetSimulatePhysics(true);
            return Body;
        }
        void Shelter(FVector P)
        {
            auto* Actor=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box); Box->SetBoxExtent(FVector(30,70,120)); Box->SetCollisionProfileName(TEXT("BlockAll"));
            Box->RegisterComponent(); Actor->SetActorLocation(P);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRiverDebrisTransport,"MessControl.River.Debris.TransportAndSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRiverDebrisTransport::RunTest(const FString&)
{
    FRiverDebrisWorld T;
    auto* Piece=T.Food(FVector(-250,-350,21));
    auto* Prop=T.Prop(FVector(-250,350,21));
    auto* Procedural=T.ProceduralPiece(FVector(-250,1150,21));
    auto* Large=T.Food(FVector(-250,950,126),FVector(2.5),9,false);
    auto* EnlargedFragment=T.Food(FVector(-250,-950,101),FVector(2),4,true);
    auto* Heavy=T.Prop(FVector(-250,-650,21),20,18);
    auto* DensePiece=T.Food(FVector(-250,-1200,21),FVector(.4),50,true);
    auto* Fixed=T.Prop(FVector(-250,650,21),20,2,false);
    auto* Dry=T.Food(FVector(1800,-1100,21));
    auto* Covered=T.Food(FVector(-250,0,21)); T.Shelter(FVector(-370,0,120));
    T.PutBodyInWave(Piece->GetActorLocation());
    TestTrue(TEXT("Small food, loose props and detached procedural bodies are eligible for the river"),AMCCoffeeFlood::IsRiverDebris(Piece->Body) && AMCCoffeeFlood::IsRiverDebris(Prop) && AMCCoffeeFlood::IsRiverDebris(Procedural));
    TestFalse(TEXT("Large whole food is not lifted by the river"),AMCCoffeeFlood::IsRiverDebris(Large->Body));
    TestFalse(TEXT("A fragment flag does not bypass its real physical size"),AMCCoffeeFlood::IsRiverDebris(EnlargedFragment->Body));
    TestTrue(TEXT("Heavy compact props and food fragments remain eligible by actual size"),AMCCoffeeFlood::IsRiverDebris(Heavy) && AMCCoffeeFlood::IsRiverDebris(DensePiece->Body));
    TestTrue(TEXT("Dense compact bodies receive a slower current instead of arbitrary mass exclusion"),T.Flood->RiverDebrisAcceleration(Heavy).Size2D()<T.Flood->RiverDebrisAcceleration(Prop).Size2D()*.8f);
    TestFalse(TEXT("The river never turns fixed scenery into a rigid body"),AMCCoffeeFlood::IsRiverDebris(Fixed));
    TestTrue(TEXT("Objects ahead of the front and behind solid cover receive no force"),T.Flood->RiverDebrisAcceleration(Dry->Body).IsNearlyZero() && T.Flood->RiverDebrisAcceleration(Covered->Body).IsNearlyZero());
    const FVector PieceStart=Piece->GetActorLocation(),PropStart=Prop->GetComponentLocation(),LargeStart=Large->GetActorLocation(),CoveredStart=Covered->GetActorLocation();
    T.Step(.7f);
    TestTrue(TEXT("Real Chaos food moves hundreds of centimetres toward the mouth"),FVector::DotProduct(Piece->GetActorLocation()-PieceStart,T.Flood->RiverDirection)>150 && Piece->bRiverSwept);
    TestTrue(TEXT("Real small loose prop is swept along the same direction"),FVector::DotProduct(Prop->GetComponentLocation()-PropStart,T.Flood->RiverDirection)>150);
    TestTrue(TEXT("A detached procedural fragment with convex physics also moves downstream"),Procedural->GetComponentLocation().X>-100 && Procedural->GetOwner()->IsReplicatingMovement());
    TestTrue(TEXT("Swept root props expose movement to connected peers"),Prop->GetOwner()->GetIsReplicated() && Prop->GetOwner()->IsReplicatingMovement());
    TestTrue(TEXT("Food preserves its existing replicated physics pose"),Piece->GetIsReplicated() && Piece->IsReplicatingMovement());
    TestTrue(TEXT("Large food and sheltered food remain where they rested"),FVector::Dist2D(LargeStart,Large->GetActorLocation())<5 && FVector::Dist2D(CoveredStart,Covered->GetActorLocation())<5 && !Large->bRiverSwept && !Covered->bRiverSwept);
    TestTrue(TEXT("Dense small fragments and props still wash, with heavy props slower than ordinary debris"),DensePiece->bRiverSwept && DensePiece->GetActorLocation().X>-200 && Heavy->GetComponentLocation().X>-200 && Heavy->GetComponentLocation().X<Prop->GetComponentLocation().X);
    TestTrue(TEXT("Dry and fixed objects never receive a flood kick"),!Dry->bRiverSwept && FMath::Abs(Dry->GetActorLocation().X-1800)<5 && !Fixed->IsSimulatingPhysics());
    T.Flood->Stop();
    TestTrue(TEXT("Stopping the event removes all further debris force"),T.Flood->RiverDebrisAcceleration(Piece->Body).IsNearlyZero());
    T.Step(1.f);
    TestTrue(TEXT("After the wave, the piece keeps its downstream position rather than returning to its spawn"),!Piece->IsDisposed() && FVector::DotProduct(Piece->GetActorLocation()-PieceStart,T.Flood->RiverDirection)>150);
    AddInfo(FString::Printf(TEXT("Food drift %.1f cm, prop drift %.1f cm"),FVector::Dist2D(PieceStart,Piece->GetActorLocation()),FVector::Dist2D(PropStart,Prop->GetComponentLocation())));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRiverDebrisDetached,"MessControl.River.Debris.DetachedPiecesAndPersistentEscape",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRiverDebrisDetached::RunTest(const FString&)
{
    FRiverDebrisWorld T;
    auto* Whole=T.Food(FVector(-250,0,21),FVector(.4),8,false); Whole->Batch=61507;
    T.PutBodyInWave(Whole->GetActorLocation()); T.Step(.15f);
    TestTrue(TEXT("A compact whole ingredient is physically swept before being cut"),Whole->bRiverSwept);
    T.Flood->Stop();
    TestTrue(TEXT("A real tool cut creates detached physical pieces"),Whole->HitFood(10000,T.Flood->RiverDirection));
    TArray<AMCFoodActor*> Pieces;
    for (TActorIterator<AMCFoodActor> It(T.World);It;++It) if (It->Batch==61507 && It->bFragment && !It->IsDisposed()) Pieces.Add(*It);
    TestEqual(TEXT("Cutting produces the configured two real fragments"),Pieces.Num(),2);
    for (auto* Piece:Pieces)
        TestTrue(TEXT("Detached pieces inherit the sweep and retain server simulation/network motion"),Piece->bRiverSwept && Piece->Body->IsSimulatingPhysics() && Piece->IsReplicatingMovement());
    if (Pieces.IsEmpty()) return false;
    auto* Washed=Pieces[0]; const FVector Escaped(2300,0,-400);
    Washed->SetActorLocation(Escaped,false,nullptr,ETeleportType::TeleportPhysics); Washed->Body->SetPhysicsLinearVelocity(FVector::ZeroVector); Washed->Tick(1.f/60);
    TestTrue(TEXT("An escaped swept fragment is removed instead of respawning upstream"),Washed->IsDisposed() && Washed->GetActorLocation().Equals(Escaped,1));
    auto* Ordinary=T.Food(FVector(-1000,300,21));
    Ordinary->SetActorLocation(FVector(-1000,300,-400),false,nullptr,ETeleportType::TeleportPhysics); Ordinary->Tick(1.f/60);
    TestTrue(TEXT("The existing rescue for unswept escaped ingredients still works"),!Ordinary->IsDisposed() && !Ordinary->bRiverSwept && FMath::IsNearlyZero(Ordinary->GetActorLocation().X) && Ordinary->GetActorLocation().Z>=Ordinary->Settings.DropHeight-1);
    auto* FreshWhole=T.Food(FVector(-250,700,21),FVector(.4),8,false); FreshWhole->Batch=61508;
    FreshWhole->HitFood(10000,T.Flood->RiverDirection);
    T.Flood->Start(T.Plan); T.PutBodyInWave(FVector(-250,700,21));
    T.Step(.7f);
    int32 SweptPieces=0;
    for (TActorIterator<AMCFoodActor> It(T.World);It;++It) if (It->Batch==61508 && It->bFragment && !It->IsDisposed())
    {
        ++SweptPieces;
        TestTrue(TEXT("New detached fragments themselves enter the river and travel to the mouth"),It->bRiverSwept && It->GetActorLocation().X>-100);
    }
    TestEqual(TEXT("The new wave sweeps both detached pieces"),SweptPieces,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRiverDebrisLegacy,"MessControl.River.Debris.LegacyFloodStillFloatsLargeFood",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRiverDebrisLegacy::RunTest(const FString&)
{
    FRiverDebrisWorld T;
    auto* Large=T.Food(FVector(-250,0,126),FVector(2.5),9,false);
    // This 250cm ingredient rests with its centre at 125cm. At the default
    // 155cm waterline its floating draft would lie below the floor, so use a
    // deep retained flood to verify the original large-food buoyancy path.
    T.Plan->FloodHeight=260;
    T.Flood->Start(T.Plan,0,true); T.Flood->WaterSettings.HoldSeconds=20; T.Flood->Seconds=T.Flood->WaterSettings.CycleSeconds();
    T.Flood->StartedAt=T.World->GetTimeSeconds()-10;
    TestFalse(TEXT("old_flood keeps the river debris field disabled"),T.Flood->bRiverFlood);
    TestTrue(TEXT("The large whole ingredient has a real simulated body"),Large->Body->IsSimulatingPhysics());
    TestFalse(TEXT("This large whole ingredient remains outside river debris eligibility"),AMCCoffeeFlood::IsRiverDebris(Large->Body));
    TestTrue(TEXT("The retained flood contains the large physical ingredient"),T.Flood->Contains(Large->GetActorLocation()));
    TestTrue(TEXT("Legacy water is deep enough to lift the whole ingredient off the floor"),
        T.Flood->SurfaceHeightAt(Large->GetActorLocation())-Large->GetActorLocation().Z-Large->Body->GetScaledBoxExtent().Z*.35f>25);
    T.Step(.3f);
    TestTrue(TEXT("The retained old flood still floats large food through its original force path"),Large->GetActorLocation().Z>135 || Large->Body->GetPhysicsLinearVelocity().Z>25);
    TestFalse(TEXT("Legacy water does not mark food as permanently river swept"),Large->bRiverSwept);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCRiverDebrisLowTongue,"MessControl.River.Debris.LowTonguePreservesFoodBeforeArrival",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCRiverDebrisLowTongue::RunTest(const FString&)
{
    FRiverDebrisWorld T; T.Flood->Stop();
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"));
    if (!TestNotNull(TEXT("Actual authored tongue mesh"),Mesh)) return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
    const FTransform Pose(FVector(0,0,-500));
    auto* Tongue=T.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose);
    Tongue->SourceMesh=Mesh; Tongue->FinishSpawning(Pose); Tongue->SetActorTickEnabled(false);
    FHitResult Hit;
    if (!TestTrue(TEXT("The low authored tongue provides a real floor"),Tongue->SurfacePoint(Tongue->Surface->Bounds.Origin,Hit))) return false;
    auto* Piece=T.Food(Hit.ImpactPoint+FVector(0,0,21)); const FVector Before=Piece->GetActorLocation();
    TestTrue(TEXT("The fixture ingredient rests below the old absolute rescue cutoff"),Before.Z<-250);
    T.Flood->Start(T.Plan); // The source begins outside the actual tongue footprint.
    TestFalse(TEXT("Food waits on dry ground ahead of the incoming wave"),T.Flood->Contains(Piece->GetActorLocation()));
    T.Step(.2f);
    TestTrue(TEXT("Before contact, food remains on the lower tongue and is not teleported or removed"),!Piece->IsDisposed() && !Piece->bRiverSwept && FVector::Dist2D(Before,Piece->GetActorLocation())<5 && Piece->GetActorLocation().Z<-250);
    T.PutBodyInWave(Piece->GetActorLocation()); T.Step(.7f);
    TestTrue(TEXT("Only actual wave arrival sweeps the piece downstream from the lower tongue"),Piece->bRiverSwept && !Piece->IsDisposed() && FVector::DotProduct(Piece->GetActorLocation()-Before,T.Flood->RiverDirection)>100);
    return true;
}
#endif
