#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FThroatContactWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    FThroatContactWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    }
    ~FThroatContactWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds)
    {for(int32 I=0;I<FMath::CeilToInt(Seconds*120);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120);}}
    USphereComponent* MovingSphere(FVector Start,FVector Velocity)
    {
        auto* Actor=World->SpawnActor<AActor>(Start,FRotator::ZeroRotator);
        auto* Body=NewObject<USphereComponent>(Actor);
        Actor->SetRootComponent(Body);Actor->AddInstanceComponent(Body);
        Body->SetMobility(EComponentMobility::Movable);Body->InitSphereRadius(25);
        Body->SetCollisionProfileName(TEXT("PhysicsActor"));Body->SetGenerateOverlapEvents(false);
        Body->SetEnableGravity(false);Body->SetLinearDamping(0);Body->SetAngularDamping(0);
        Body->RegisterComponent();Actor->SetActorLocation(Start);
        Body->SetSimulatePhysics(true);Body->SetUseCCD(true);Body->SetPhysicsLinearVelocity(Velocity);
        return Body;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatTissueContactTest,"MessControl.Throat.AuthoredTissueContact.BlocksBodiesAndLeavesHoleOpen",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatTissueContactTest::RunTest(const FString&)
{
    auto* ArtistMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FromBlender4/SK_Exit.SK_Exit"));
    auto* ContactMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/ManualIntake/SM_ThroatTissueContact.SM_ThroatTissueContact"));
    if(!TestNotNull(TEXT("Saved artist throat is available"),ArtistMesh)
        || !TestNotNull(TEXT("Saved posed contact mesh is available"),ContactMesh)) return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({ContactMesh});
#endif
    FThroatContactWorld T;
    if(!TestNotNull(TEXT("The fixture owns a real physics scene"),T.World->GetPhysicsScene())) return false;
    // A rotated/translated actor catches an accidental world-space mesh bake or
    // contact attachment that fails to follow AuthoredMouth's transform.
    const FTransform Pose(FRotator(0,37,0),FVector(6000,0,5000));
    auto* Throat=T.World->SpawnActorDeferred<AMCThroat>(AMCThroat::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Throat->bManualIntake=true;Throat->ManualAuthoredExpansion=.75f;Throat->bReverseAuthoredOpen=true;
    Throat->AuthoredMouth->SetSkeletalMesh(ArtistMesh);Throat->FinishSpawning(Pose);
    T.Step(1.f/60);
    if(!TestNotNull(TEXT("Native tissue contact component exists"),Throat->TissueContact.Get())) return false;
    TestTrue(TEXT("The native component references the saved cooked-contact asset"),Throat->TissueContact->GetStaticMesh()==ContactMesh);
    TestFalse(TEXT("Contact triangles do not render"),Throat->TissueContact->IsVisible());
    TestTrue(TEXT("Contact triangles remain hidden during play"),Throat->TissueContact->bHiddenInGame);

    // These component-local probes cross the left/right tissue and the
    // measured Open=.25 aperture. Use simple gameplay sweeps,
    // rather than complex line traces that could conceal missing body contact.
    // Actual saved-map visible aim, verified against the posed render-data bake.
    const FVector LocalCenter(.000028,285.618088,-239.899164),LocalNormal(0,.9747185974,.2234360218);
    const FTransform Art=Throat->AuthoredMouth->GetComponentTransform();
    const FVector Inward=Art.TransformVectorNoScale(LocalNormal).GetSafeNormal();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCThroatContactTest),false);
    // The legacy closed gate is a separate mechanic. Isolate the new surface
    // without ignoring its owner (which would also ignore TissueContact).
    TArray<UPrimitiveComponent*> Components;Throat->GetComponents(Components);
    for(auto* Component:Components) if(Component!=Throat->TissueContact.Get()) Query.AddIgnoredComponent(Component);
    constexpr float Radius=25,Speed=1200;
    const FCollisionShape Shape=FCollisionShape::MakeSphere(Radius);
    auto Sweep=[&](FVector LocalPoint,ECollisionChannel Channel,FHitResult& Hit)
    {
        const FVector Point=Art.TransformPosition(LocalPoint);
        return T.World->SweepSingleByChannel(Hit,Point-Inward*1200,Point+Inward*1200,FQuat::Identity,Channel,Shape,Query);
    };

    for(float Side:{-650.f,650.f}) {
        const FVector LocalPoint=LocalCenter+FVector(Side,0,0);
        FHitResult PawnHit,BodyHit,CameraHit;
        const FString SideName=Side<0?TEXT("Left"):TEXT("Right");
        const bool PawnBlocked=Sweep(LocalPoint,ECC_Pawn,PawnHit);
        const bool BodyBlocked=Sweep(LocalPoint,ECC_PhysicsBody,BodyHit);
        TestTrue(*FString::Printf(TEXT("%s tissue blocks a simple Pawn sphere sweep"),*SideName),PawnBlocked && PawnHit.GetComponent()==Throat->TissueContact.Get());
        TestTrue(*FString::Printf(TEXT("%s tissue blocks a simple PhysicsBody sphere sweep"),*SideName),BodyBlocked && BodyHit.GetComponent()==Throat->TissueContact.Get());
        TestFalse(*FString::Printf(TEXT("%s tissue does not retract the gameplay camera"),*SideName),Sweep(LocalPoint,ECC_Camera,CameraHit));
        if(!BodyBlocked || BodyHit.GetComponent()!=Throat->TissueContact.Get()) continue;

        const FVector Start=Art.TransformPosition(LocalPoint)-Inward*1200;
        auto* Body=T.MovingSphere(Start,Inward*Speed);
        if(TestTrue(TEXT("The incoming test sphere actually simulates physics"),Body->IsSimulatingPhysics())) {
            const float Duration=BodyHit.Distance/Speed+.10f;
            T.Step(Duration);
            const float Progress=FVector::DotProduct(Body->GetComponentLocation()-Start,Inward);
            TestTrue(*FString::Printf(TEXT("%s tissue physically interrupts incoming motion (progress %.1f, free %.1f)"),*SideName,Progress,Speed*Duration),
                Progress<Speed*Duration-30);
        }
        Body->GetOwner()->Destroy();
    }

    for(ECollisionChannel Channel:{ECC_Pawn,ECC_PhysicsBody}) {
        FHitResult Hit;
        TestFalse(TEXT("The real aperture permits a 25 cm gameplay sphere"),Sweep(LocalCenter,Channel,Hit));
    }
    const FVector HoleStart=Art.TransformPosition(LocalCenter)-Inward*1200;
    auto* ThroughHole=T.MovingSphere(HoleStart,Inward*Speed);
    if(TestTrue(TEXT("The aperture probe actually simulates physics"),ThroughHole->IsSimulatingPhysics())) {
        T.Step(2.2f);
        TestTrue(TEXT("A physical sphere passes all the way through the real opening"),
            FVector::DotProduct(ThroughHole->GetComponentLocation()-HoleStart,Inward)>2400);
    }
    ThroughHole->GetOwner()->Destroy();

    // Reticle visibility needs the production floor clamp. Without a tongue,
    // VacuumInlet legitimately targets the buried centre of the full aperture.
    // A real AMCTongue built from the engine plane supplies the measured map
    // floor height through its ordinary SurfacePoint collision query.
    auto* FloorMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(!TestNotNull(TEXT("Tongue-floor fixture mesh is available"),FloorMesh)) return false;
#if WITH_EDITOR
    FStaticMeshCompilingManager::Get().FinishCompilation({FloorMesh});
#endif
    const float FloorZ=Art.TransformPosition(FVector(0,0,-392.1871)).Z;
    const FTransform FloorPose(FRotator::ZeroRotator,FVector(6000,0,FloorZ),FVector(100,100,1));
    auto* Tongue=T.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Tongue->SourceMesh=FloorMesh;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(FloorPose);Tongue->SetActorTickEnabled(false);
    FHitResult FloorHit;
    if(!TestTrue(TEXT("The reticle fixture has a real native tongue collision surface"),
        Tongue->SurfacePoint(Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter),FloorHit))) return false;
    TestTrue(TEXT("The native tongue collision supplies the measured arena floor height"),FMath::IsNearlyEqual(FloorHit.ImpactPoint.Z,double(FloorZ),.1));

    FVector AimCenter;float AimRadius=0;
    if(TestTrue(TEXT("The native intake supplies its real camera target"),Throat->GetIntakeAimTarget(AimCenter,AimRadius))) {
        TestTrue(TEXT("Native floor clamping selects the measured visible upper aperture"),
            Art.InverseTransformPosition(AimCenter).Equals(LocalCenter,.1));
        const FVector Right=Art.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
        // Independently verify each ray against the world before asserting the
        // public reticle result; this catches ignoring the whole throat actor.
        for(float Side:{-1.f,1.f}) {
            bool FoundOccludedView=false;
            for(float Lateral:{900.f,1400.f,2200.f}) {
                for(float Depth:{-400.f,0.f,400.f}) {
                    const FVector Origin=AimCenter+Right*(Side*Lateral)+Inward*Depth;
                    FHitResult Occlusion;
                    if(!T.World->LineTraceSingleByChannel(Occlusion,Origin,AimCenter,ECC_Visibility,Query)
                        || Occlusion.GetComponent()!=Throat->TissueContact.Get()) continue;
                    FoundOccludedView=true;
                    TestEqual(TEXT("Tissue that physically occludes the hole also hides the reticle"),
                        Throat->IntakeAimStrength(Origin,(AimCenter-Origin).GetSafeNormal()),0.f);
                    break;
                }
                if(FoundOccludedView) break;
            }
            TestTrue(TEXT("A real side-tissue occlusion ray exists in the saved contact mesh"),FoundOccludedView);
        }
        // A gameplay camera approaches above the tongue. Following the tilted
        // aperture normal backwards instead would put it below the floor.
        const FVector ClearOrigin=AimCenter-Inward.GetSafeNormal2D()*1200+FVector(0,0,100);
        FHitResult Occlusion;
        const bool Clear=!T.World->LineTraceSingleByChannel(Occlusion,ClearOrigin,AimCenter,ECC_Visibility,Query);
        if(TestTrue(TEXT("A camera ray through the actual native target is clear"),Clear))
            TestTrue(TEXT("The unoccluded hole still reveals the reticle"),
                Throat->IntakeAimStrength(ClearOrigin,(AimCenter-ClearOrigin).GetSafeNormal())>.99f);
    }

    Throat->bManualIntake=false;Throat->RebuildAppearance();
    Query.AddIgnoredActor(Tongue); // The final query isolates contact disabling, not floor collision.
    TestTrue(TEXT("Legacy intake disables the extra tissue collider"),Throat->TissueContact->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    for(float Side:{-650.f,650.f}) {
        FHitResult Hit;
        TestFalse(TEXT("Legacy mode removes the new surface from real gameplay queries"),Sweep(LocalCenter+FVector(Side,0,0),ECC_PhysicsBody,Hit));
    }
    return true;
}
#endif
