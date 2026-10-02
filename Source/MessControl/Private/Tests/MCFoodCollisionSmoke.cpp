#if !UE_BUILD_SHIPPING
#include "MCFoodActor.h"
#include "MCFoodBodyComponent.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "MCThroat.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/Convex.h"
#include "DrawDebugHelpers.h"
#include "StaticMeshResources.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#include "ShaderCompiler.h"
#endif

namespace
{
    double CollisionVisibleBottom(const AMCFoodActor* Food)
    {
        const auto* Data=Food->ItemMesh->GetRenderData();
        if (!Data || Data->LODResources.IsEmpty()) return MAX_dbl;
        const auto& V=Data->LODResources[0].VertexBuffers.PositionVertexBuffer;
        double Bottom=MAX_dbl; const FTransform T=Food->Visual->GetComponentTransform();
        for(uint32 I=0;I<V.GetNumVertices();++I) Bottom=FMath::Min(Bottom,T.TransformPosition(FVector(V.VertexPosition(I))).Z);
        return Bottom;
    }
    void CollisionWire(UWorld* World,AMCFoodActor* Food)
    {
        const auto* Setup=Food->Body->GetBodySetup();if(!Setup) return;
        const FTransform T=Food->Body->GetComponentTransform();
        for(const auto& Hull:Setup->AggGeom.ConvexElems) if(const auto& Convex=Hull.GetChaosConvexMesh())
            for(int32 I=0;I<Convex->NumEdges();++I)
                DrawDebugLine(World,T.TransformPosition(FVector(Convex->GetVertex(Convex->GetEdgeVertex(I,0)))),
                    T.TransformPosition(FVector(Convex->GetVertex(Convex->GetEdgeVertex(I,1)))),FColor(65,255,220),false,0,0,2);
    }
}

// A reproducible render-target take: real x20 bodies, gravity, two swept probes,
// then an overlay drawn from the same cooked Chaos hulls used by physics.
void MCTickFoodCollisionValidation(UWorld* W)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;TWeakObjectPtr<ACameraActor> Camera;
        TArray<TWeakObjectPtr<AMCFoodActor>> Foods;
        TWeakObjectPtr<AActor> Probe;TWeakObjectPtr<USphereComponent> Sphere;
        TWeakObjectPtr<UStaticMeshComponent> ProbeMesh;
        FVector P,Focus,ClearStart,ClearEnd,SolidStart,SolidEnd;
        double At=0;float Width=0,Radius=0;int32 Stage=0;
        bool Started=false,Failed=false,ClearBlocked=false,SolidBlocked=false;
    };static FRun R;if(R.World!=W) {R=FRun();R.World=W;}
    auto* GS=W->GetGameState<AMCGameState>();auto* PC=W->GetFirstPlayerController();
    if(!GS || !PC || W->GetTimeSeconds()<4) return;
#if WITH_EDITOR
    if(!R.Started && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto Check=[&](bool OK,const TCHAR* Text){R.Failed|=!OK;UE_LOG(LogTemp,Display,TEXT("MC_FOOD_COLLISION_CHECK %s %s"),OK?TEXT("PASS"):TEXT("FAIL"),Text);};
    auto Finish=[&](){UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s FOOD_COLLISION"),R.Failed?TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);};
    auto View=[&](FVector Focus,float Width,float Angle=0.f){
        const FVector Offset=FRotator(0,Angle,0).RotateVector(FVector(-Width*.95f,-Width*1.35f,Width*.8f));
        R.Camera->SetActorLocation(Focus+Offset);R.Camera->SetActorRotation((-Offset).Rotation());PC->SetViewTarget(R.Camera.Get());
    };
    auto Title=[&](const TCHAR* Text){GS->CurrentEvent->Title=FText::FromString(Text);};
    if(!R.Started) {
        const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Menu=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Collision take")):nullptr;
        if(!Menu) {Check(false,TEXT("artist egg menu exists"));Finish();return;}
        R.Started=true;R.At=W->GetTimeSeconds();R.P=FVector(0,0,9000);
        if(auto* Mode=W->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        GS->bDevManualEvents=true;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;
        GS->DayPlan=nullptr;GS->CurrentEvent=NewObject<UMCDayEvent>(GS);
        Title(TEXT("ЕДА ×20 · ЦЕЛАЯ ФОРМА И ФРАГМЕНТ"));
        for(TActorIterator<AMCThroat> It(W);It;++It) It->SetActorTickEnabled(false);
        for(TActorIterator<AMCTongue> It(W);It;++It) It->bAutomaticYawns=false;
        if(auto* H=Cast<AMCToothCharacter>(PC->GetPawn())) H->CancelGameplayInput();
        FMCFoodRow Row=*Menu;
        Row.WholeMeshes={TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/Meshes/Breakfast/SM_Egg_01.SM_Egg_01")))};
        Row.FragmentMeshes={TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/Meshes/Breakfast/SM_Egg_03.SM_Egg_03")))};
        UStaticMesh* Whole=Row.WholeMeshes[0].LoadSynchronous();UStaticMesh* Fragment=Row.FragmentMeshes[0].LoadSynchronous();
        if(!Whole || !Fragment) {Check(false,TEXT("whole and cut artist egg meshes load"));Finish();return;}
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Whole,Fragment});
#endif
        const FVector E=Whole->GetBounds().BoxExtent*Row.Scale*20,F=Fragment->GetBounds().BoxExtent*Row.FragmentScale*20;
        const float Distance=E.GetMax()+F.GetMax()+300;
        R.Width=Distance+E.GetMax()+F.GetMax()+600;
        for(bool Part:{false,true}) {
            const FTransform T(FRotator(Part?-9:17,Part?-17:29,Part?33:13),R.P+FVector(Part?Distance*.5:-Distance*.5,0,E.GetMax()+900),FVector(20));
            auto* Food=W->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            FRandomStream Random(7);Food->ConfigureItem(TEXT("Egg"),Row,Random,Part);Food->FinishSpawning(T);
            Food->Body->SetEnableGravity(false);Food->SetActorTickEnabled(false);Food->Label->SetVisibility(false);
            R.Foods.Add(Food);
            auto* Body=Cast<UMCFoodBodyComponent>(Food->Body);
            Check(Body && Body->HasMeshCollision() && Body->GetBodySetup()->AggGeom.BoxElems.IsEmpty(),TEXT("x20 food uses authored cooked convex shapes, no bounding box"));
        }
        auto* Floor=W->SpawnActor<AActor>();auto* Plate=NewObject<UStaticMeshComponent>(Floor);
        Floor->SetRootComponent(Plate);Plate->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Plate->SetCollisionProfileName(TEXT("BlockAll"));Plate->RegisterComponent();
        Floor->SetActorScale3D(FVector(R.Width/50,R.Width/65,.4));Floor->SetActorLocation(R.P-FVector(0,0,20));
        for(TActorIterator<AMCTongue> It(W);It;++It) {if(It->Surface->GetNumMaterials()>0) Plate->SetMaterial(0,It->Surface->GetMaterial(0));break;}
        R.Camera=W->SpawnActor<ACameraActor>();R.Camera->GetCameraComponent()->SetFieldOfView(48);
        R.Focus=R.P+FVector(0,0,E.GetMax()*.6+450);View(R.Focus,R.Width);
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_COLLISION_START scale=20 whole=%s fragment=%s width=%.1f"),*E.ToCompactString(),*F.ToCompactString(),R.Width);
    }
    const double T=W->GetTimeSeconds()-R.At;
    if(T>1.5 && R.Stage==0) {
        Title(TEXT("ЕДА ×20 · ПАДЕНИЕ И КОНТАКТ С ПОЛОМ"));
        for(const auto& Food:R.Foods) {Food->Body->SetEnableGravity(true);Food->Body->WakeAllRigidBodies();}
        ++R.Stage;
    }
    if(T>6.5 && R.Stage==1) {
        for(const auto& Food:R.Foods) {
            const double Bottom=CollisionVisibleBottom(Food.Get());
            Check(!Food->GetActorLocation().ContainsNaN() && Food->Body->GetPhysicsLinearVelocity().Size()<10,TEXT("whole and fragment settle under real Chaos gravity"));
            Check(Bottom>=R.P.Z-2 && Bottom<=R.P.Z+FMath::Max(3.,Food->Visual->Bounds.BoxExtent.GetMax()*.025),TEXT("visible mesh rests on floor without a floating bounding-box gap"));
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_COLLISION_REST fragment=%d bottom=%.3f floor=%.3f velocity=%s"),Food->bFragment,Bottom,R.P.Z,*Food->Body->GetPhysicsLinearVelocity().ToCompactString());
        }
        auto* Food=R.Foods[0].Get();const FVector E=Food->Body->GetUnscaledBoxExtent();
        R.Radius=FMath::Max(10.f,float((E*Food->GetActorScale3D()).GetMin()*.06));
        FCollisionQueryParams Complex(SCENE_QUERY_STAT(FoodCollisionTake),true);bool Found=false;
        for(int32 Axis=0;Axis<2 && !Found;++Axis) for(float A:{-.92f,.92f}) for(float B:{.92f,-.92f}) {
            if(Found) continue;FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
            Start[Axis]=-E[Axis]*1.5;End[Axis]=E[Axis]*1.5;
            Start[(Axis+1)%3]=End[(Axis+1)%3]=E[(Axis+1)%3]*A;
            Start[(Axis+2)%3]=End[(Axis+2)%3]=E[(Axis+2)%3]*B;
            Start=Food->GetActorTransform().TransformPosition(Start);End=Food->GetActorTransform().TransformPosition(End);
            if(FMath::Min(Start.Z,End.Z)<R.P.Z+R.Radius+5) continue;
            FHitResult Hit;
            if(Food->GripSurface->LineTraceComponent(Hit,Start,End,Complex)
                || Food->GripSurface->SweepComponent(Hit,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(R.Radius),true)
                || Food->Body->SweepComponent(Hit,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(R.Radius),false)) continue;
            R.ClearStart=Start;R.ClearEnd=End;Found=true;
        }
        Check(Found,TEXT("visible empty corner admits a sphere through the live cooked collider"));
        if(!Found) {Finish();return;}
        const FVector LocalStart(-E.X*1.5,0,0),LocalEnd(E.X*1.5,0,0);
        R.SolidStart=Food->GetActorTransform().TransformPosition(LocalStart);R.SolidEnd=Food->GetActorTransform().TransformPosition(LocalEnd);
        R.Probe=W->SpawnActor<AActor>();R.Sphere=NewObject<USphereComponent>(R.Probe.Get());R.Probe->SetRootComponent(R.Sphere.Get());
        R.Sphere->InitSphereRadius(R.Radius);R.Sphere->SetCollisionProfileName(TEXT("PhysicsActor"));R.Sphere->RegisterComponent();
        R.ProbeMesh=NewObject<UStaticMeshComponent>(R.Probe.Get());R.ProbeMesh->SetupAttachment(R.Sphere.Get());
        R.ProbeMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        R.ProbeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);R.ProbeMesh->SetRelativeScale3D(FVector(R.Radius/50));R.ProbeMesh->RegisterComponent();
        R.Probe->SetActorLocation(R.ClearStart);R.Focus=Food->Visual->Bounds.Origin;R.Width=Food->Visual->Bounds.BoxExtent.GetMax()*2.4f;
        View(R.Focus,R.Width);Title(TEXT("ПУСТОЙ УГОЛ · ШАР ПРОХОДИТ МИМО ФОРМЫ"));++R.Stage;
    }
    if(T>7 && T<9.5 && R.Stage==2) {
        FHitResult Hit;R.Probe->SetActorLocation(FMath::Lerp(R.ClearStart,R.ClearEnd,float((T-7)/2.5)),true,&Hit);
        R.ClearBlocked|=Hit.bBlockingHit;
        DrawDebugLine(W,R.ClearStart,R.ClearEnd,FColor(80,255,130),false,0,0,3);
    }
    if(T>=9.5 && R.Stage==2) {
        Check(!R.ClearBlocked && FVector::Dist(R.Probe->GetActorLocation(),R.ClearEnd)<R.Radius*2,TEXT("swept physical sphere crosses an empty bounding corner without phantom contact"));
        R.Probe->SetActorLocation(R.SolidStart);Title(TEXT("ЗАНЯТАЯ ПОВЕРХНОСТЬ · ШАР ОСТАНАВЛИВАЕТСЯ"));++R.Stage;
    }
    if(T>10 && T<12.5 && R.Stage==3) {
        FHitResult Hit;R.Probe->SetActorLocation(FMath::Lerp(R.SolidStart,R.SolidEnd,float((T-10)/2.5)),true,&Hit);
        R.SolidBlocked|=Hit.bBlockingHit && Hit.GetActor()==R.Foods[0].Get();
    }
    if(T>=12.5 && R.Stage==3) {
        Check(R.SolidBlocked,TEXT("the same swept sphere stops on occupied food surface"));
        R.Probe->SetActorHiddenInGame(true);Title(TEXT("КОНТУР · РЕАЛЬНЫЕ CONVEX КОЛЛАЙДЕРЫ ×20"));
        R.Focus=(R.Foods[0]->Visual->Bounds.Origin+R.Foods[1]->Visual->Bounds.Origin)*.5;
        R.Width=FVector::Dist(R.Foods[0]->GetActorLocation(),R.Foods[1]->GetActorLocation())+R.Foods[0]->Visual->Bounds.BoxExtent.GetMax()*2.3;
        ++R.Stage;
    }
    if(R.Stage==4) {View(R.Focus,R.Width,float((T-12.5)*5));for(const auto& Food:R.Foods) CollisionWire(W,Food.Get());}
    if(T>17) Finish();
}
#endif
