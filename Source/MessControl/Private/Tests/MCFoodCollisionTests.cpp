#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshResources.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
    // A plain physics world keeps mouth boundaries, day events and swallowing out
    // of collision tests. All probes below use live Chaos shapes, not OnHit mocks.
    struct FFoodCollisionWorld
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        FFoodCollisionWorld()
        {
            auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        }
        ~FFoodCollisionWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
        void Step(float Seconds)
        {
            for (int32 I=0;I<FMath::CeilToInt(Seconds*60);++I)
            { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); }
        }
        AMCFoodActor* Food(const FMCFoodRow& Row,bool Fragment,const FTransform& Transform)
        {
            auto* Actor=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,
                nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            FRandomStream Random(7); Actor->ConfigureItem(TEXT("CollisionFixture"),Row,Random,Fragment);
            Actor->FinishSpawning(Transform); Actor->SetActorTickEnabled(false);
            Actor->Body->SetEnableGravity(false);
            return Actor;
        }
    };

    UStaticMesh* CollisionMesh(const TSoftObjectPtr<UStaticMesh>& Choice)
    {
        auto* Mesh=Choice.LoadSynchronous();
#if WITH_EDITOR
        if (Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        return Mesh;
    }
    bool EggRow(FAutomationTestBase& Test,FMCFoodRow& Row)
    {
        auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        if (!Test.TestNotNull(TEXT("Current food menu"),Table)) return false;
        const auto* Menu=Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Food collision"));
        if (!Test.TestNotNull(TEXT("Artist egg menu row"),Menu)) return false;
        Row=*Menu;
        UStaticMesh* Whole=nullptr; UStaticMesh* Fragment=nullptr;
        for (const auto& Choice:Row.WholeMeshes) if (!Choice.IsNull()) { Whole=CollisionMesh(Choice); if (Whole) break; }
        for (const auto& Choice:Row.FragmentMeshes) if (!Choice.IsNull()) { Fragment=CollisionMesh(Choice); if (Fragment) break; }
        if (!Test.TestNotNull(TEXT("Whole artist egg mesh"),Whole)
            || !Test.TestNotNull(TEXT("Separate artist egg fragment mesh"),Fragment)) return false;
        Test.TestTrue(TEXT("Fragments use a different mesh, not a shrunken whole egg"),Whole!=Fragment);
        Row.WholeMeshes={Whole}; Row.FragmentMeshes={Fragment}; Row.SpoilSeconds=180;
        return true;
    }
    bool CookedConvex(FAutomationTestBase& Test,AMCFoodActor* Food,const FString& Name)
    {
        auto* Body=Cast<UMCFoodBodyComponent>(Food->Body);
        if (!Test.TestNotNull(*(Name+TEXT(" uses the mesh physics component")),Body)) return false;
        auto* Setup=Body->GetBodySetup();
        if (!Test.TestNotNull(*(Name+TEXT(" has a body setup")),Setup)) return false;
        Test.TestEqual(*(Name+TEXT(" has no bounding box physics shape")),Setup->AggGeom.BoxElems.Num(),0);
        Test.TestTrue(*(Name+TEXT(" has convex collision")),!Setup->AggGeom.ConvexElems.IsEmpty());
        Test.TestTrue(*(Name+TEXT(" has registered physics")),Body->IsPhysicsStateCreated());
        return !Setup->AggGeom.ConvexElems.IsEmpty();
    }
    bool Ray(UPrimitiveComponent* Component,FVector Start,FVector End,bool Complex,FHitResult& Hit)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(FoodCollisionTest),Complex);
        return Component->LineTraceComponent(Hit,Start,End,Params);
    }
    bool Sphere(UPrimitiveComponent* Component,FVector Start,FVector End,float Radius,bool Complex,FHitResult& Hit)
    {
        return Component->SweepComponent(Hit,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(Radius),Complex);
    }
    struct FFoodProbe
    {
        FVector Start,End;
        float Radius=0;
    };
    TArray<FFoodProbe> CornerProbes(AMCFoodActor* Food)
    {
        const FVector E=Food->ItemMesh->GetBounds().BoxExtent
            *(Food->bFragment?Food->FoodData.FragmentScale:Food->FoodData.Scale);
        const FTransform T=Food->Body->GetComponentTransform();
        const FVector Scale=T.GetScale3D().GetAbs();
        const float Radius=FMath::Max(.05f,float((E*Scale).GetMin()*.025));
        TArray<FFoodProbe> Result;
        for (int32 Axis=0;Axis<3;++Axis) for (float A:{-.92f,.92f}) for (float B:{-.92f,.92f})
        {
            FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
            Start[Axis]=-E[Axis]*1.5; End[Axis]=E[Axis]*1.5;
            Start[(Axis+1)%3]=End[(Axis+1)%3]=E[(Axis+1)%3]*A;
            Start[(Axis+2)%3]=End[(Axis+2)%3]=E[(Axis+2)%3]*B;
            Result.Add({T.TransformPosition(Start),T.TransformPosition(End),Radius});
        }
        return Result;
    }
    double LowestVisibleVertex(AMCFoodActor* Food)
    {
        const auto* Render=Food->ItemMesh->GetRenderData();
        if (!Render || Render->LODResources.IsEmpty()) return TNumericLimits<double>::Max();
        const auto& Positions=Render->LODResources[0].VertexBuffers.PositionVertexBuffer;
        double Bottom=TNumericLimits<double>::Max();
        const FTransform T=Food->Visual->GetComponentTransform();
        for (uint32 I=0;I<Positions.GetNumVertices();++I)
            Bottom=FMath::Min(Bottom,T.TransformPosition(FVector(Positions.VertexPosition(I))).Z);
        return Bottom;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodCollisionQueriesTest,"MessControl.Food.Collision.MeshQueriesAtTwentyTimesScale",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodCollisionQueriesTest::RunTest(const FString&)
{
    FMCFoodRow Row; if (!EggRow(*this,Row)) return false;
    TestEqual(TEXT("Exact contact fixture uses the artist whole egg"),Row.WholeMeshes[0].GetAssetName(),FString(TEXT("SM_Egg_01")));
    // Item and actor scale are independent. The rotated, nonuniform case catches
    // baking a world AABB or applying either scale twice to a cooked convex hull.
    Row.Scale*=FVector(.8,1.15,.65); Row.FragmentScale*=FVector(1.2,.75,1.1);
    const TArray<FTransform> Transforms={
        FTransform(FQuat::Identity,FVector(0,0,2000),FVector::OneVector),
        FTransform(FQuat::Identity,FVector(10000,0,2000),FVector(20)),
        FTransform(FRotator(23,47,-19),FVector(20000,0,2000),FVector(20,13,27))};
    FFoodCollisionWorld Fixture;
    for (bool Fragment:{false,true}) for (int32 Case=0;Case<Transforms.Num();++Case)
    {
        auto* Food=Fixture.Food(Row,Fragment,Transforms[Case]);
        Food->Body->SetSimulatePhysics(false);
        const FString Name=FString::Printf(TEXT("%s transform %d"),Fragment?TEXT("fragment"):TEXT("whole"),Case);
        if (!CookedConvex(*this,Food,Name)) continue;
        const FVector E=Food->Body->GetUnscaledBoxExtent();
        TestTrue(*(Name+TEXT(" retains exact mesh bounding dimensions without a minimum box")),
            E.Equals(Food->ItemMesh->GetBounds().BoxExtent*(Fragment?Food->FoodData.FragmentScale:Food->FoodData.Scale),.01));
        int32 Occupied=0;
        for (int32 Axis=0;Axis<3;++Axis)
        {
            FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
            Start[Axis]=-E[Axis]*1.5; End[Axis]=E[Axis]*1.5;
            Start=Food->Body->GetComponentTransform().TransformPosition(Start);
            End=Food->Body->GetComponentTransform().TransformPosition(End);
            FHitResult Visible,Physical;
            if (Ray(Food->GripSurface,Start,End,true,Visible))
            {
                ++Occupied;
                const bool PhysicalHit=Ray(Food->Body,Start,End,false,Physical);
                TestTrue(*(Name+TEXT(" simple ray hits the occupied visible centre")),PhysicalHit);
                if (PhysicalHit && !Fragment && Food->ItemMesh->GetFName()==TEXT("SM_Egg_01"))
                {
                    const double ContactGap=FVector::Dist(Physical.ImpactPoint,Visible.ImpactPoint);
                    TestTrue(*FString::Printf(TEXT("%s axis %d exact egg collider contact is within 1 cm of the visible mesh"),*Name,Axis),ContactGap<1.);
                    AddInfo(FString::Printf(TEXT("%s axis=%d exact contact gap=%.6f cm physical=%s visible=%s normal dot=%.6f"),
                        *Name,Axis,ContactGap,*Physical.ImpactPoint.ToCompactString(),*Visible.ImpactPoint.ToCompactString(),
                        FVector::DotProduct(Physical.ImpactNormal,Visible.ImpactNormal)));
                }
                TestTrue(*(Name+TEXT(" sphere sweep hits the occupied visible centre")),Sphere(Food->Body,Start,End,.1f,false,Physical));
            }
        }
        TestTrue(*(Name+TEXT(" has a visible occupied centre probe")),Occupied>0);
        int32 VisibleEmpty=0,PhysicalEmpty=0;
        for (const auto& Probe:CornerProbes(Food))
        {
            FHitResult Visible,Physical;
            // The complex mesh is an independent oracle for empty outer corners.
            // Convex approximation may bridge concavities, so require multiple
            // genuinely clear outer routes rather than exact triangle parity.
            if (Ray(Food->GripSurface,Probe.Start,Probe.End,true,Visible)
                || Sphere(Food->GripSurface,Probe.Start,Probe.End,Probe.Radius,true,Visible)) continue;
            ++VisibleEmpty;
            if (!Ray(Food->Body,Probe.Start,Probe.End,false,Physical)
                && !Sphere(Food->Body,Probe.Start,Probe.End,Probe.Radius,false,Physical)) ++PhysicalEmpty;
        }
        TestTrue(*(Name+TEXT(" exposes empty visible bounding corners")),VisibleEmpty>=2);
        TestTrue(*(Name+TEXT(" rays and spheres can pass at least two empty corners; a box cannot")),PhysicalEmpty>=2);
        AddInfo(FString::Printf(TEXT("%s simple empty corners=%d/%d extent=%s actor scale=%s"),
            *Name,PhysicalEmpty,VisibleEmpty,*E.ToCompactString(),*Food->GetActorScale3D().ToCompactString()));
        Food->Destroy();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodCollisionMenuTest,"MessControl.Food.Collision.MenuVariantsHaveIndependentConvexBodies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodCollisionMenuTest::RunTest(const FString&)
{
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if (!TestNotNull(TEXT("Current artist menu"),Table)) return false;
    FFoodCollisionWorld Fixture; int32 Checked=0,Expected=0;
    for (const auto& Entry:Table->GetRowMap()) for (bool Fragment:{false,true})
    {
        const auto& Menu=*reinterpret_cast<const FMCFoodRow*>(Entry.Value);
        const auto& Choices=Fragment?Menu.FragmentMeshes:Menu.WholeMeshes;
        for (const auto& Choice:Choices)
        {
            if (Choice.IsNull()) continue;
            ++Expected;
            auto* Mesh=CollisionMesh(Choice);
            const FString Name=Entry.Key.ToString()+TEXT(" ")+Choice.GetAssetName()+(Fragment?TEXT(" fragment"):TEXT(" whole"));
            if (!TestNotNull(*Name,Mesh)) continue;
            FMCFoodRow Row=Menu; Row.WholeMeshes={Mesh}; Row.FragmentMeshes={Mesh};
            auto* Food=Fixture.Food(Row,Fragment,FTransform(FVector(0,0,1000)));
            if (CookedConvex(*this,Food,Name))
            {
                ++Checked;
                TestTrue(*(Name+TEXT(" owns its body setup without mutating the source mesh")),Food->Body->GetBodySetup()!=Mesh->GetBodySetup());
                for (const auto& Hull:Food->Body->GetBodySetup()->AggGeom.ConvexElems)
                    TestTrue(*(Name+TEXT(" has a nondegenerate convex hull")),Hull.VertexData.Num()>=4);
            }
            Food->Destroy();
        }
    }
    TestTrue(TEXT("Menu contains whole and fragment artist variants"),Expected>0);
    TestEqual(TEXT("Every configured non-null whole and fragment mesh choice has a convex body"),Checked,Expected);
    AddInfo(FString::Printf(TEXT("Checked %d cooked artist food variant bodies"),Checked));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodCollisionDropTest,"MessControl.Food.Collision.DropAndRestAtTwentyTimesScale",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodCollisionDropTest::RunTest(const FString&)
{
    FMCFoodRow Row; if (!EggRow(*this,Row)) return false;
    FFoodCollisionWorld Fixture;
    auto* Floor=Fixture.World->SpawnActor<AActor>();
    auto* Plane=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Plane);
    Plane->SetBoxExtent(FVector(15000,15000,20)); Plane->SetCollisionProfileName(TEXT("BlockAll"));
    Plane->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-20));
    for (bool Fragment:{false,true})
    {
        const FString Name=Fragment?TEXT("fragment x20"):TEXT("whole x20");
        auto* Food=Fixture.Food(Row,Fragment,FTransform(FRotator(17,29,13),FVector(Fragment?3000:0,0,1500),FVector(20)));
        if (!CookedConvex(*this,Food,Name)) continue;
        Food->SetActorLocation(FVector(Fragment?3000:0,0,Food->Body->Bounds.BoxExtent.Z+800),false,nullptr,ETeleportType::TeleportPhysics);
        const double InitialCentre=Food->GetActorLocation().Z;
        const double InitialBottom=LowestVisibleVertex(Food);
        TestTrue(*(Name+TEXT(" starts visibly above the floor")),InitialBottom>300 && FMath::IsFinite(InitialBottom));
        Food->Body->SetEnableGravity(true); Food->Body->WakeAllRigidBodies();
        Fixture.Step(8);
        const FVector Position=Food->GetActorLocation(),Velocity=Food->Body->GetPhysicsLinearVelocity();
        const double Bottom=LowestVisibleVertex(Food);
        TestTrue(*(Name+TEXT(" falls under actual gravity")),Position.Z<InitialCentre-300);
        TestTrue(*(Name+TEXT(" settles with finite transform and velocity")),!Position.ContainsNaN() && !Velocity.ContainsNaN() && Velocity.Size()<5);
        TestTrue(*(Name+TEXT(" visible mesh does not penetrate the supporting floor")),Bottom>=-2);
        TestTrue(*(Name+TEXT(" rests on its visible surface without hovering on a bounding box")),Bottom<=FMath::Max(3.,Food->Visual->Bounds.BoxExtent.GetMax()*.025));
        const FVector Before=Food->GetActorLocation(); Fixture.Step(.5f);
        TestTrue(*(Name+TEXT(" stays at rest after contact")),FVector::Dist(Before,Food->GetActorLocation())<2);
        AddInfo(FString::Printf(TEXT("%s bottom=%.3f final=%s velocity=%s"),*Name,Bottom,*Position.ToCompactString(),*Velocity.ToCompactString()));
        Food->Destroy();
    }
    // Exercise production fracture at the same scale as the drop cases.
    // A unique batch excludes unrelated actors from fragment counting.
    const FTransform FracturePose(FRotator(21,53,-17),FVector(10000,12000,5000),FVector(20));
    auto* Breakable=Fixture.Food(Row,false,FracturePose);Breakable->Batch=74020;
    const FQuat ExpectedRotation=Breakable->GetActorQuat();
    TestTrue(TEXT("Actual damage fractures the rotated x20 whole food"),Breakable->HitFood(10000,FVector::ForwardVector));
    TestTrue(TEXT("Fractured parent is disposed"),Breakable->IsDisposed());
    int32 Pieces=0;
    for (TActorIterator<AMCFoodActor> It(Fixture.World);It;++It)
        if (It->Batch==74020 && It->bFragment && !It->IsDisposed())
        {
            ++Pieces;
            const FString Name=FString::Printf(TEXT("damage fragment %d"),Pieces);
            TestTrue(*(Name+TEXT(" inherits actor scale x20")),It->GetActorScale3D().Equals(FVector(20),.01));
            TestTrue(*(Name+TEXT(" inherits parent actor rotation")),It->GetActorQuat().Equals(ExpectedRotation,.001));
            TestTrue(*(Name+TEXT(" uses the configured cut mesh and independent fragment scale")),
                It->ItemMesh==Row.FragmentMeshes[0].Get() && It->Visual->GetRelativeScale3D().Equals(It->FoodData.FragmentScale,.01));
            CookedConvex(*this,*It,Name);
            TestTrue(*(Name+TEXT(" is a simulated fragment with finite motion")),It->Body->IsSimulatingPhysics()
                && !It->Body->GetPhysicsLinearVelocity().ContainsNaN());
        }
    TestEqual(TEXT("Actual damage creates all configured fragments"),Pieces,Row.Fragments);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodCollisionReplicationTest,"MessControl.Food.Collision.ReplicatedMeshRebuild",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodCollisionReplicationTest::RunTest(const FString&)
{
    FMCFoodRow Row; if (!EggRow(*this,Row)) return false;
    FFoodCollisionWorld Fixture;
    const FTransform Transform(FRotator(-14,31,8),FVector(0,0,2000),FVector(20,14,23));
    auto* Source=Fixture.Food(Row,false,Transform);
    auto* Replica=Fixture.Food(Row,true,Transform);
    for (bool Fragment:{false,true})
    {
        FRandomStream Random(7); Source->ConfigureItem(TEXT("CollisionFixture"),Row,Random,Fragment);
        Source->Body->SetSimulatePhysics(false); Replica->Body->SetSimulatePhysics(false);
        // Exercise the actual RepNotify after replacing its replicated payload.
        // This is a local rebuild regression; transport is covered by network smoke.
        Replica->FoodData=Source->FoodData; Replica->bFragment=Source->bFragment; Replica->ItemMesh=Source->ItemMesh;
        Replica->ProcessEvent(Replica->FindFunctionChecked(TEXT("OnRep_Item")),nullptr);
        const FString Name=Fragment?TEXT("replicated fragment"):TEXT("replicated whole");
        if (!CookedConvex(*this,Replica,Name)) continue;
        TestTrue(*(Name+TEXT(" restores the mesh and independent item scale")),Replica->Visual->GetStaticMesh()==Source->ItemMesh
            && Replica->Visual->GetRelativeScale3D().Equals(Source->Visual->GetRelativeScale3D()));
        TestTrue(*(Name+TEXT(" has the same bounding dimensions")),Replica->Body->GetUnscaledBoxExtent().Equals(Source->Body->GetUnscaledBoxExtent(),.01));
        TestEqual(*(Name+TEXT(" has the same convex hull count")),Replica->Body->GetBodySetup()->AggGeom.ConvexElems.Num(),Source->Body->GetBodySetup()->AggGeom.ConvexElems.Num());
        for (const auto& Probe:CornerProbes(Source))
        {
            FHitResult A,B;
            TestEqual(*(Name+TEXT(" rebuilds the same live simple ray query")),Ray(Replica->Body,Probe.Start,Probe.End,false,A),Ray(Source->Body,Probe.Start,Probe.End,false,B));
            TestEqual(*(Name+TEXT(" rebuilds the same live sphere sweep query")),Sphere(Replica->Body,Probe.Start,Probe.End,Probe.Radius,false,A),Sphere(Source->Body,Probe.Start,Probe.End,Probe.Radius,false,B));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodCollisionBudgetTest,"MessControl.Food.Collision.CurrentMenuCompoundChaosBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodCollisionBudgetTest::RunTest(const FString&)
{
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
    if (!TestNotNull(TEXT("Performance fixture loads the current saved menu"),Table)) return false;
    const auto* Menu=Table->FindRow<FMCFoodRow>(TEXT("Bacon"),TEXT("Food collision budget"));
    if (!TestNotNull(TEXT("Performance fixture uses the current Bacon row"),Menu)) return false;
    FMCFoodRow Row=*Menu;
    TSoftObjectPtr<UStaticMesh> GreenChoice;
    for (const auto& Choice:Row.WholeMeshes)
        if (Choice.GetAssetName()==TEXT("SM_GrapesGreen")) { GreenChoice=Choice; break; }
    TestTrue(TEXT("Current Bacon menu includes the artist green grape compound"),!GreenChoice.IsNull());
    if (GreenChoice.IsNull()) return false;
    const double SourceLoadStarted=FPlatformTime::Seconds();
    UStaticMesh* Mesh=CollisionMesh(GreenChoice);
    const double SourceLoadMs=(FPlatformTime::Seconds()-SourceLoadStarted)*1000;
    if (!TestNotNull(TEXT("Green grape source mesh"),Mesh)) return false;
    auto* SourceSetup=Mesh->GetBodySetup();
    if (!TestNotNull(TEXT("Green grape saved source collision"),SourceSetup)) return false;
    const bool SourceWasCooked=SourceSetup->bCreatedPhysicsMeshes;
    const double SourceCookStarted=FPlatformTime::Seconds();
    SourceSetup->CreatePhysicsMeshes();
    const double SourceCookMs=(FPlatformTime::Seconds()-SourceCookStarted)*1000;
    TestTrue(TEXT("Current Bacon item scale is x20 independently of actor scale"),Row.Scale.Equals(FVector(20),.01));
    Row.WholeMeshes={Mesh}; // Keep every other saved gameplay/physics value unchanged.
    const auto* Render=Mesh->GetRenderData();
    if (!TestTrue(TEXT("Performance fixture has actual LOD0 source geometry"),Render && !Render->LODResources.IsEmpty())) return false;
    const auto& LOD=Render->LODResources[0];
    const uint32 RenderVertices=LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices();
    const uint32 RenderTriangles=LOD.IndexBuffer.GetNumIndices()/3;
    int32 SourceHullVertices=0;
    for (const auto& Hull:SourceSetup->AggGeom.ConvexElems) SourceHullVertices+=Hull.VertexData.Num();

    FFoodCollisionWorld Fixture;
    auto* Floor=Fixture.World->SpawnActor<AActor>();
    auto* Plane=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Plane);
    Plane->SetBoxExtent(FVector(15000,15000,20)); Plane->SetCollisionProfileName(TEXT("BlockAll"));
    Plane->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-20));
    FString CSV=TEXT("phase,frame,world_tick_ms\n");
    FString Report=FString::Printf(TEXT("mesh=%s\nitem_scale=%s\nactor_scale=1,1,1\nsource_render_vertices=%u\nsource_render_triangles=%u\nsource_hulls=%d\nsource_hull_vertices=%d\nsource_load_compile_ms=%.3f\nsource_physics_meshes_were_cached=%d\nsource_create_physics_meshes_ms=%.3f\n"),
        *Mesh->GetPathName(),*Row.Scale.ToCompactString(),RenderVertices,RenderTriangles,
        SourceSetup->AggGeom.ConvexElems.Num(),SourceHullVertices,SourceLoadMs,SourceWasCooked,SourceCookMs);
    UE_LOG(LogTemp,Display,TEXT("MC_FOOD_COLLISION_BUDGET source_hulls=%d source_vertices=%d render_vertices=%u render_triangles=%u source_load_ms=%.3f source_create_physics_ms=%.3f cached=%d"),
        SourceSetup->AggGeom.ConvexElems.Num(),SourceHullVertices,RenderVertices,RenderTriangles,SourceLoadMs,SourceCookMs,SourceWasCooked);
    auto Tick=[&](const TCHAR* Phase,int32 Frame,TArray<double>& Samples)
    {
        ++GFrameCounter;
        const double Started=FPlatformTime::Seconds();
        // UWorld tick completes TG_EndPhysics. This times real Chaos/contact work,
        // not mocked OnHit callbacks or rendering/present/asset loading.
        Fixture.World->Tick(LEVELTICK_All,1.f/60);
        const double Ms=(FPlatformTime::Seconds()-Started)*1000;
        Samples.Add(Ms); CSV+=FString::Printf(TEXT("%s,%d,%.6f\n"),Phase,Frame,Ms);
    };
    auto Summarize=[&](const TCHAR* Phase,TArray<double> Samples)
    {
        Samples.Sort(); double Sum=0; for (double Ms:Samples) Sum+=Ms;
        const FString Line=FString::Printf(TEXT("phase=%s frames=%d mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f"),
            Phase,Samples.Num(),Sum/Samples.Num(),Samples[Samples.Num()/2],
            Samples[FMath::Min(Samples.Num()-1,FMath::FloorToInt(Samples.Num()*.95))],Samples.Last());
        AddInfo(Line); Report+=Line+TEXT("\n");
    };
    Fixture.Step(.25f);
    TArray<double> Baseline;
    for (int32 I=0;I<60;++I) Tick(TEXT("floor_only"),I,Baseline);
    Summarize(TEXT("floor_only"),Baseline);
    const FPlatformMemoryStats MemoryBefore=FPlatformMemory::GetStats();
    TArray<AMCFoodActor*> Foods; TArray<FVector> StartPositions;
    int32 TotalCookedHulls=0,TotalLiveShapes=0,TotalCookedVertices=0;
    for (int32 I=0;I<4;++I)
    {
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_COLLISION_BUDGET body=%d begin_cook_and_register"),I);
        const double BodyStarted=FPlatformTime::Seconds();
        const float X=I%2?1800.f:-1800.f,Y=I/2?1800.f:-1800.f;
        auto* Food=Fixture.Food(Row,false,FTransform(FRotator(11+I*5,I*29,7-I*3),FVector(X,Y,2000),FVector::OneVector));
        const double BodyMs=(FPlatformTime::Seconds()-BodyStarted)*1000;
        const FString Name=FString::Printf(TEXT("current Bacon Green body %d"),I);
        if (!CookedConvex(*this,Food,Name)) return false;
        TestTrue(*(Name+TEXT(" retains ActorScale=1")),Food->GetActorScale3D().Equals(FVector::OneVector));
        auto* Setup=Food->Body->GetBodySetup();
        int32 CookedVertices=0; for (const auto& Hull:Setup->AggGeom.ConvexElems) CookedVertices+=Hull.VertexData.Num();
        int32 LiveShapes=0;
        auto* Instance=Food->Body->GetBodyInstance();
        if (!TestNotNull(*(Name+TEXT(" has a live body instance")),Instance)) return false;
        FPhysicsCommand::ExecuteRead(Instance->GetPhysicsActor(),[&](const FPhysicsActorHandle&)
        {
            TArray<FPhysicsShapeHandle> Shapes;
            Instance->GetAllShapes_AssumesLocked(Shapes); LiveShapes=Shapes.Num();
        });
        TestEqual(*(Name+TEXT(" cooks every saved source hull")),Setup->AggGeom.ConvexElems.Num(),SourceSetup->AggGeom.ConvexElems.Num());
        TestEqual(*(Name+TEXT(" registers every cooked hull as a live Chaos shape")),LiveShapes,Setup->AggGeom.ConvexElems.Num());
        TotalCookedHulls+=Setup->AggGeom.ConvexElems.Num(); TotalLiveShapes+=LiveShapes; TotalCookedVertices+=CookedVertices;
        const FString Line=FString::Printf(TEXT("body=%d runtime_body_cook_and_register_ms=%.3f cooked_hulls=%d live_chaos_shapes=%d cooked_vertices=%d"),
            I,BodyMs,Setup->AggGeom.ConvexElems.Num(),LiveShapes,CookedVertices);
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_COLLISION_BUDGET %s"),*Line);
        AddInfo(Line); Report+=Line+TEXT("\n");
        Food->SetActorLocation(FVector(X,Y,Food->Body->Bounds.BoxExtent.Z+500),false,nullptr,ETeleportType::TeleportPhysics);
        TestTrue(*(Name+TEXT(" begins above the supporting floor")),LowestVisibleVertex(Food)>300);
        Foods.Add(Food); StartPositions.Add(Food->GetActorLocation());
    }
    // All four heavy compounds fall and contact the same real floor concurrently.
    // They start separated so initial overlap/depenetration cannot fake the budget.
    for (auto* Food:Foods) { Food->Body->SetEnableGravity(true); Food->Body->WakeAllRigidBodies(); }
    TArray<double> Active;
    for (int32 I=0;I<180;++I)
    {
        Tick(TEXT("four_compounds_drop_contact"),I,Active);
        for (auto* Food:Foods)
            if (!TestTrue(TEXT("Heavy compound simulation stays finite during every measured tick"),
                !Food->GetActorTransform().ContainsNaN() && !Food->Body->GetPhysicsLinearVelocity().ContainsNaN()
                && !Food->Body->GetPhysicsAngularVelocityInRadians().ContainsNaN())) return false;
    }
    Summarize(TEXT("four_compounds_drop_contact"),Active);
    TArray<double> Settling;
    for (int32 I=0;I<300;++I) Tick(TEXT("four_compounds_settling"),I,Settling);
    Summarize(TEXT("four_compounds_settling"),Settling);
    TArray<FVector> RestPositions;
    for (int32 I=0;I<Foods.Num();++I)
    {
        auto* Food=Foods[I]; const FString Name=FString::Printf(TEXT("heavy Green compound %d"),I);
        const FVector Position=Food->GetActorLocation(),Velocity=Food->Body->GetPhysicsLinearVelocity();
        const double Bottom=LowestVisibleVertex(Food);
        TestTrue(*(Name+TEXT(" falls under actual gravity")),Position.Z<StartPositions[I].Z-250);
        TestTrue(*(Name+TEXT(" settles with a finite transform and velocity")),!Food->GetActorTransform().ContainsNaN() && !Velocity.ContainsNaN() && Velocity.Size()<5);
        TestTrue(*(Name+TEXT(" visible mesh stays above its supporting floor")),FMath::IsFinite(Bottom) && Bottom>=-2);
        TestTrue(*(Name+TEXT(" visible geometry rests near contact without a bounding-box hover")),Bottom<=FMath::Max(3.,Food->Visual->Bounds.BoxExtent.GetMax()*.025));
        RestPositions.Add(Position);
        Report+=FString::Printf(TEXT("body=%d final_bottom_cm=%.6f final_velocity_cm_s=%.6f\n"),I,Bottom,Velocity.Size());
    }
    Fixture.Step(.5f);
    for (int32 I=0;I<Foods.Num();++I)
        TestTrue(*FString::Printf(TEXT("Heavy Green compound %d remains at rest after contact"),I),FVector::Dist(RestPositions[I],Foods[I]->GetActorLocation())<2);
    const FPlatformMemoryStats MemoryAfter=FPlatformMemory::GetStats();
    Report+=FString::Printf(TEXT("bodies=4\ntotal_cooked_hulls=%d\ntotal_live_chaos_shapes=%d\ntotal_cooked_vertices=%d\nprocess_physical_mib_before=%.3f\nprocess_physical_mib_after=%.3f\nprocess_virtual_mib_before=%.3f\nprocess_virtual_mib_after=%.3f\nmeasurement_scope=CPU world tick including completed Chaos simulation; excludes rendering and body creation\nbudget_threshold=none; platform timings are diagnostic evidence\n"),
        TotalCookedHulls,TotalLiveShapes,TotalCookedVertices,MemoryBefore.UsedPhysical/double(1024*1024),MemoryAfter.UsedPhysical/double(1024*1024),
        MemoryBefore.UsedVirtual/double(1024*1024),MemoryAfter.UsedVirtual/double(1024*1024));
    AddInfo(Report);
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("TestReports/FoodCollisionPerformance");
    IFileManager::Get().MakeDirectory(*Folder,true);
    TestTrue(TEXT("Writes measured Chaos tick samples"),FFileHelper::SaveStringToFile(CSV,*(Folder/TEXT("Ticks.csv"))));
    TestTrue(TEXT("Writes geometry, actual shape and cook/registration evidence"),FFileHelper::SaveStringToFile(Report,*(Folder/TEXT("Results.txt"))));
    return true;
}
#endif
