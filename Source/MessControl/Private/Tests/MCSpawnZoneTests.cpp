#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCTongue.h"
#include "MCFoodActor.h"
#include "MCArenaTooth.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FSpawnZoneWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    FSpawnZoneWorld(bool Authored=false)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,Authored?TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"):TEXT("/Engine/BasicShapes/Plane.Plane"));
#if WITH_EDITOR
        if(Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        // Match the saved L_Mouth actor: spawn depth is measured in world X,
        // while the imported source tongue's long axis is local Y.
        const FTransform Pose(Authored?FRotator(0,90.637834,0).Quaternion():FQuat::Identity,
            FVector(10000,0,1000),Authored?FVector(1.875,1.5,1):FVector(30,20,1));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose);
        Tongue->SourceMesh=Mesh;Tongue->FinishSpawning(Pose);Tongue->SetActorTickEnabled(false);
    }
    ~FSpawnZoneWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    FVector AtDepth(double Depth,double Y=0) const
    {
        const FBox B=Tongue->Surface->Bounds.GetBox();
        return FVector(B.Max.X-B.GetSize().X*Depth,B.GetCenter().Y+Y,B.GetCenter().Z);
    }
};

// Independent geometric check: dense rim samples also catch a footprint that
// crosses a forbidden boundary between the sampler's own collision probes.
bool EntireAllowedBand(const AMCTongue* Tongue,FVector Center,float Margin)
{
    const FBox B=Tongue->Surface->Bounds.GetBox();
    const double MinX=B.Max.X-B.GetSize().X*Tongue->GameplaySpawnFarDepth;
    const double MaxX=B.Max.X-B.GetSize().X*Tongue->GameplaySpawnNearDepth;
    for(int32 I=0;I<64;++I) {
        const double Angle=I*2*PI/64;
        const FVector P=Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Margin;
        if(P.X<MinX-.01 || P.X>MaxX+.01) return false;
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSpawnZoneWeightedTest,"MessControl.SpawnZones.WeightedRegionsAndDeterminism",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSpawnZoneWeightedTest::RunTest(const FString&)
{
    FSpawnZoneWorld A,B;
    if(!TestTrue(TEXT("Fixture builds a real queryable procedural tongue"),!A.Tongue->CurrentVertices().IsEmpty())) return false;
    FRandomStream RA(41),RB(41);int32 Counts[2]={0,0};
    for(int32 I=0;I<2048;++I) {
        FHitResult HA,HB;int32 ZA=-1,ZB=-1;
        const bool FoundA=A.Tongue->RandomGameplaySpawnPoint(RA,40,0,{},HA,&ZA);
        const bool FoundB=B.Tongue->RandomGameplaySpawnPoint(RB,40,0,{},HB,&ZB);
        if(!FoundA || !FoundB || ZA<0 || ZA>1 || ZA!=ZB || !HA.ImpactPoint.Equals(HB.ImpactPoint,.001)) {
            AddError(FString::Printf(TEXT("Seeded placements diverged or failed at sample %d"),I));return false;
        }
        ++Counts[ZA];
        if(A.Tongue->GameplaySpawnZone(HA.ImpactPoint)!=ZA || !EntireAllowedBand(A.Tongue,HA.ImpactPoint,40)) {
            AddError(TEXT("A sampled center or footprint escaped its allowed region"));return false;
        }
    }
    const float Left=float(Counts[0])/2048;
    TestTrue(*FString::Printf(TEXT("Wide region receives 30 percent, independent of its larger area (observed %.3f)"),Left),FMath::Abs(Left-.30f)<.035f);
    TestEqual(TEXT("Throat end is forbidden"),A.Tongue->GameplaySpawnZone(A.AtDepth(.05)),-1);
    TestEqual(TEXT("Front end is forbidden"),A.Tongue->GameplaySpawnZone(A.AtDepth(.95)),-1);
    FHitResult Hit;
    TestFalse(TEXT("A center inside the band cannot let its footprint spill over the throat boundary"),A.Tongue->GameplaySpawnFootprint(A.AtDepth(.18)+FVector(-10,0,0),40,Hit));
    TestFalse(TEXT("A center inside the band cannot let its footprint spill over the front boundary"),A.Tongue->GameplaySpawnFootprint(A.AtDepth(.82)+FVector(10,0,0),40,Hit));
    TestFalse(TEXT("Stuck food also retains the throat end strip"),A.Tongue->GameplayStuckFoodFootprint(A.AtDepth(.18)+FVector(-10,0,0),40,Hit));
    TestFalse(TEXT("Stuck food also retains the front end strip"),A.Tongue->GameplayStuckFoodFootprint(A.AtDepth(.82)+FVector(10,0,0),40,Hit));
    TestTrue(TEXT("Crossing the internal split remains allowed"),A.Tongue->GameplaySpawnFootprint(A.AtDepth(.60),80,Hit));
    TestFalse(TEXT("Negative margin is rejected"),A.Tongue->RandomGameplaySpawnPoint(RA,-1,0,{},Hit));
    TestFalse(TEXT("Negative separation is rejected"),A.Tongue->RandomGameplaySpawnPoint(RA,40,-1,{},Hit));
    TestFalse(TEXT("An oversized footprint fails safely"),A.Tongue->RandomGameplaySpawnPoint(RA,10000,0,{},Hit));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSpawnZoneBlockedTest,"MessControl.SpawnZones.BlockedRegionDoesNotRedistribute",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSpawnZoneBlockedTest::RunTest(const FString&)
{
    FSpawnZoneWorld T;const FBox Bounds=T.Tongue->Surface->Bounds.GetBox();
    const double Split=T.AtDepth(.60).X,Near=T.AtDepth(.18).X;
    TArray<FVector> Excluded;
    for(double X=Split;X<Near+100;X+=100)
        for(double Y=Bounds.Min.Y;Y<Bounds.Max.Y+100;Y+=100) Excluded.Add(FVector(X,Y,Bounds.GetCenter().Z));
    FRandomStream Random(73);FHitResult Hit;int32 Zone=-1;
    T.Tongue->GameplaySpawnLeftChance=1;
    TestFalse(TEXT("Fully occupied selected wide region refuses a spawn instead of moving it to the narrow region"),T.Tongue->RandomGameplaySpawnPoint(Random,40,75,Excluded,Hit,&Zone));
    T.Tongue->GameplaySpawnLeftChance=0;
    TestTrue(TEXT("The other region remains usable with the same exclusions"),T.Tongue->RandomGameplaySpawnPoint(Random,40,75,Excluded,Hit,&Zone));
    TestEqual(TEXT("Explicit narrow selection stays narrow"),Zone,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSpawnZoneAuthoredTest,"MessControl.SpawnZones.AuthoredSurfaceAndDeliveryFootprints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSpawnZoneAuthoredTest::RunTest(const FString&)
{
    FSpawnZoneWorld T(true);FHitResult Hit;
    if(!TestTrue(TEXT("Authored tongue provides a real collision floor"),T.Tongue->SurfacePoint(T.AtDepth(.4),Hit))) return false;
    const FBox Bounds=T.Tongue->Surface->Bounds.GetBox();
    auto* Cap=T.World->SpawnActor<AMCFoodDisposal>(FVector(Bounds.Max.X-150,Bounds.GetCenter().Y,Bounds.Max.Z+50),FRotator::ZeroRotator);
    Cap->Volume->SetBoxExtent(FVector(Bounds.GetSize().X*.3,Bounds.GetSize().Y,500));Cap->SetActorTickEnabled(false);
    TArray<FVector> Outer,Inner;
    if(!TestTrue(TEXT("Delivery exclusion uses the tongue's real curved cap outline"),Cap->GetDeliveryZoneOutline(Outer,Inner))) return false;
    const int32 Mid=Inner.Num()/2;const FVector InnerEdge=Inner[Mid];
    FVector JustOutside=InnerEdge-FVector(20,0,0);FHitResult Floor;
    if(!TestTrue(TEXT("The cap edge has tissue beneath it"),T.Tongue->SurfacePoint(JustOutside,Floor))) return false;
    TestFalse(TEXT("Footprint center is outside the cap"),Cap->ContainsDeliveryPosition(Floor.ImpactPoint+FVector(0,0,35)));
    TestFalse(TEXT("A footprint reaching into the actual delivery cap is rejected"),T.Tongue->GameplaySpawnFootprint(Floor.ImpactPoint,70,Hit));
    TestFalse(TEXT("Stuck food still cannot overlap the throat cap"),T.Tongue->GameplayStuckFoodFootprint(Floor.ImpactPoint,70,Hit));

    auto* BrushCap=T.World->SpawnActor<AMCFoodDisposal>(FVector(Bounds.Min.X+150,Bounds.GetCenter().Y,Bounds.Max.Z+50),FRotator::ZeroRotator);
    BrushCap->bBrushBin=true;
    BrushCap->Volume->SetBoxExtent(FVector(Bounds.GetSize().X*.3,Bounds.GetSize().Y,500));BrushCap->SetActorTickEnabled(false);
    TArray<FVector> BrushOuter,BrushInner;
    if(!TestTrue(TEXT("Brush exit has its real curved cap"),BrushCap->GetDeliveryZoneOutline(BrushOuter,BrushInner))) return false;
    const FVector BrushPoint=BrushInner[BrushInner.Num()/2]+FVector(20,0,0);
    if(!TestTrue(TEXT("Brush cap contact has a supported floor"),T.Tongue->SurfacePoint(BrushPoint,Floor))) return false;
    TestFalse(TEXT("Normal food still excludes the brush cap footprint"),T.Tongue->GameplaySpawnFootprint(Floor.ImpactPoint,70,Hit));
    TestTrue(TEXT("A tooth-anchored jam may touch the brush cap while retaining its whole supported footprint"),T.Tongue->GameplayStuckFoodFootprint(Floor.ImpactPoint,70,Hit));

    const FVector InsideBrush=BrushInner[BrushInner.Num()/2]-FVector(30,0,0);
    if(!TestTrue(TEXT("Jammed-food delivery fixture has a floor"),T.Tongue->SurfacePoint(InsideBrush,Floor))) return false;
    auto* FoodMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
    if(FoodMesh) FStaticMeshCompilingManager::Get().FinishCompilation({FoodMesh});
#endif
    if(!TestNotNull(TEXT("Jammed-food fixture mesh"),FoodMesh)) return false;
    FMCFoodRow Row;Row.Scale=FVector(.25);Row.HalfExtent=FVector(12.5);Row.Health=100;
    Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FoodMesh));
    FRandomStream FoodRandom(87);
    const FTransform FoodPose(Floor.ImpactPoint+FVector(0,0,18));
    auto* Food=T.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),FoodPose);
    Food->ConfigureItem(TEXT("Fibre"),Row,FoodRandom);Food->Initialize(true,FVector::ForwardVector);
    Food->Phase=EMCFoodPhase::Stuck;
    Food->StuckTooth=T.World->SpawnActor<AMCArenaTooth>(FoodPose.GetLocation(),FRotator::ZeroRotator);
    Food->FinishSpawning(FoodPose);
    const FVector JammedLocation=Food->GetActorLocation();
    TestTrue(TEXT("Jammed fixture actually lies in the brush exit cap"),BrushCap->ContainsDeliveryPosition(JammedLocation));
    BrushCap->Tick(.1f);
    TestTrue(TEXT("Brush delivery preserves the jam until a real tool hit"),!Food->IsDisposed() && Food->Phase==EMCFoodPhase::Stuck && Food->GetActorLocation().Equals(JammedLocation,.01));
    TestTrue(TEXT("A nonlethal tool hit frees the actual jammed item"),Food->HitFood(1,FVector::ForwardVector));
    BrushCap->Tick(.1f);
    TestTrue(TEXT("Freed fresh food survives and is returned outside the brush cap"),!Food->IsDisposed() && Food->Phase==EMCFoodPhase::Free && !BrushCap->ContainsDeliveryPosition(Food->GetActorLocation()));
    Food->Dispose();
    BrushCap->Destroy();
    FRandomStream Random(41);int32 Found[2]={0,0};
    for(int32 I=0;I<256;++I) {
        const float Margin=I%2?55.f*1.415f+40:175.f*1.415f+40;
        if(!T.Tongue->RandomGameplaySpawnPoint(Random,Margin,0,{},Hit)) continue;
        ++Found[I%2];
        if(!EntireAllowedBand(T.Tongue,Hit.ImpactPoint,Margin)) {AddError(TEXT("Authored stain footprint crossed an end strip"));return false;}
        for(int32 J=0;J<64;++J) {
            const float Angle=J*2*PI/64;FHitResult Rim;
            const FVector P=Hit.ImpactPoint+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Margin;
            if(!T.Tongue->SurfacePoint(P,Rim) || Cap->ContainsDeliveryPosition(Rim.ImpactPoint+FVector(0,0,35))) {
                AddError(TEXT("Authored stain footprint left tissue or entered a delivery cap"));return false;
            }
        }
    }
    TestTrue(TEXT("Small coffee footprints still find usable authored tissue"),Found[1]>30);
    TestTrue(TEXT("Large coffee footprints still find usable authored tissue"),Found[0]>30);
    return true;
}
#endif
