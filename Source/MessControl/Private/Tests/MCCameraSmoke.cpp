#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void MCTickCameraValidation(UWorld* World)
{
    struct FRun {TWeakObjectPtr<UWorld> World;float Age=0,StageAt=0,MinWalkX=0,MinEyeX=MAX_flt,MaxEyeX=-MAX_flt;FVector DeadZoneEye=FVector::ZeroVector;int32 Stage=-1,Seen=0,Samples=0,Waypoint=0;bool Invalid=false,Shot=false,DeadZoneMoved=false;};
    static FRun R;if(R.World!=World){R=FRun();R.World=World;}
    R.Age+=World->GetDeltaSeconds();
    auto* PC=World->GetFirstPlayerController();auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=World->GetGameState<AMCGameState>();
    if(!H || !GS || R.Age<3)return;
    GS->Phase=EMCShiftPhase::Intermission;GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    const FVector Places[]={FVector(-400,-500,0),FVector(-40,-620,0),FVector(500,-550,0),FVector(-400,500,0),FVector(500,550,0),FVector(850,0,0),FVector(900,0,330),
        FVector(-990,0,0),FVector(-750,-400,0),FVector(-750,400,0),FVector(-550,-650,0),FVector(-550,650,0),FVector(-500,0,0)};
    // Follow the curved walkable tongue rim instead of walking into the gap
    // outside it. Death/falling is a separate scenario, not an overview route.
    const FVector Route[]={FVector(-990,0,0),FVector(-650,0,0),FVector(-450,-500,0),FVector(300,-550,0),
        FVector(300,550,0),FVector(-450,500,0),FVector(-650,0,0),FVector(-990,0,0),FVector(-500,0,0)};
    const bool Walking=R.Stage==UE_ARRAY_COUNT(Places)-1;
    if(R.Stage<0 || R.Age-R.StageAt>(Walking?28.f:1.8f) || (Walking && R.Waypoint==UE_ARRAY_COUNT(Route))){
        ++R.Stage;R.StageAt=R.Age;R.Shot=false;
        if(R.Stage>=UE_ARRAY_COUNT(Places)){
            const bool Pass=!R.Invalid && R.Seen==UE_ARRAY_COUNT(Places) && R.Samples>300 && R.MinWalkX<-950 && R.Waypoint==UE_ARRAY_COUNT(Route) && R.MaxEyeX-R.MinEyeX>150 && R.DeadZoneMoved;
            UE_LOG(LogTemp,Display,TEXT("MC_CAMERA_%s viewpoints=%d screenSamples=%d nearEdgeX=%.1f route=%d/9 followTravel=%.1f deadZone=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),R.Seen,R.Samples,R.MinWalkX,R.Waypoint,R.MaxEyeX-R.MinEyeX,R.DeadZoneMoved);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
        }
        FVector P=Places[R.Stage];
        if(R.Stage!=6)for(TActorIterator<AMCTongue> It(World);It;++It){FHitResult Hit;if(It->SurfacePoint(P,Hit))P.Z=Hit.ImpactPoint.Z+H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2;break;}
        H->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
        H->bMouthCameraInitialized=false;
        H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->DisableMovement();
        if(R.Stage==UE_ARRAY_COUNT(Places)-1) H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
    if(R.Stage==0 && R.Age-R.StageAt>.6f && !R.DeadZoneMoved) {
        R.DeadZoneEye=H->MouthCameraEye;
        H->SetActorLocation(H->GetActorLocation()+FVector(25,25,25),false,nullptr,ETeleportType::TeleportPhysics);
        R.DeadZoneMoved=true;
    }
    if(R.Stage==0 && R.DeadZoneMoved && R.Age-R.StageAt>.8f && !H->MouthCameraEye.Equals(R.DeadZoneEye,.1)) {
        R.Invalid=true;UE_LOG(LogTemp,Error,TEXT("MC_CAMERA_FAIL movement inside dead zone moved the camera"));
    }
    if(R.Stage==UE_ARRAY_COUNT(Places)-1) {
        if(R.Waypoint<UE_ARRAY_COUNT(Route) && FVector::Dist2D(Route[R.Waypoint],H->GetActorLocation())<30)++R.Waypoint;
        if(R.Waypoint<UE_ARRAY_COUNT(Route))H->AddMovementInput((Route[R.Waypoint]-H->GetActorLocation()).GetSafeNormal2D());
        R.MinWalkX=FMath::Min(R.MinWalkX,float(H->GetActorLocation().X));
        R.MinEyeX=FMath::Min(R.MinEyeX,float(H->Camera->GetComponentLocation().X));R.MaxEyeX=FMath::Max(R.MaxEyeX,float(H->Camera->GetComponentLocation().X));
        if(!H->Status->IsAlive()) {R.Invalid=true;UE_LOG(LogTemp,Error,TEXT("MC_CAMERA_FAIL walking actor died at %s"),*H->GetActorLocation().ToString());}
    }
    if(R.Age-R.StageAt>.25f) {
        int32 W=0,V=0;PC->GetViewportSize(W,V);
        const FVector Eye=H->Camera->GetComponentLocation(),P=H->GetActorLocation();
        bool InFrame=true;
        for(int32 I=0;I<8;++I) {
            const FVector Corner=P+FVector(I&1?50:-50,I&2?50:-50,I&4?82:-82);
            if(W>0 && V>0) {
                FVector2D Screen;
                InFrame &= PC->ProjectWorldLocationToScreen(Corner,Screen) && Screen.X>W*.025f && Screen.X<W*.975f && Screen.Y>V*.025f && Screen.Y<V*.975f;
            } else {
                const FVector Local=H->Camera->GetComponentRotation().UnrotateVector(Corner-Eye);
                const float TanX=FMath::Tan(FMath::DegreesToRadians(H->Camera->FieldOfView*.5f)),TanY=TanX/(16.f/9.f);
                InFrame &= Local.X>0 && FMath::Abs(Local.Y)<Local.X*TanX*.95 && FMath::Abs(Local.Z)<Local.X*TanY*.95;
            }
        }
        const auto* Box=H->MouthCameraBounds.IsValid()?H->MouthCameraBounds->FindComponentByClass<UBoxComponent>():nullptr;
        const FVector LocalEye=Box?Box->GetComponentTransform().InverseTransformPosition(Eye):FVector::ZeroVector;
        const bool Bounded=Box && FBox(-Box->GetUnscaledBoxExtent(),Box->GetUnscaledBoxExtent()).IsInsideOrOn(LocalEye);
        if((!InFrame || !Bounded) && !R.Invalid) UE_LOG(LogTemp,Error,TEXT("MC_CAMERA_FAIL stage=%d inFrame=%d bounded=%d pawn=%s eye=%s"),R.Stage,InFrame,Bounded,*P.ToString(),*Eye.ToString());
        R.Invalid|=!InFrame || !Bounded; ++R.Samples;
    }
    if(!R.Shot && R.Age-R.StageAt>1.5f){
        const FVector Eye=H->Camera->GetComponentLocation();bool Blocked=false;
        for(TActorIterator<AActor> It(World);It;++It)if(auto* C=It->FindComponentByClass<UStaticMeshComponent>();C && C->GetStaticMesh() && C->GetStaticMesh()->GetName()==TEXT("SM_Roof_Wall")){
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCCameraShell),true,H);
            Blocked|=C->LineTraceComponent(Hit,H->GetActorLocation()+FVector(0,0,75),Eye,Q);
        }
        const float Facing=FVector::DotProduct(H->Camera->GetForwardVector(),(H->GetActorLocation()-Eye).GetSafeNormal());
        R.Invalid|=Blocked || Eye.ContainsNaN() || !H->CameraBoom->bDoCollisionTest;
        UE_LOG(LogTemp,Display,TEXT("MC_CAMERA_VIEW %d blocked=%d facing=%.3f eye=%s"),R.Stage,Blocked,Facing,*Eye.ToString());
        if(FParse::Param(FCommandLine::Get(),TEXT("MCCameraCapture"))){
            const FString Folder=FPaths::ProjectDir()/TEXT("Artifacts/Camera");IFileManager::Get().MakeDirectory(*Folder,true);
            FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("View%02d.png"),R.Stage),true,false);
        }
        R.Shot=true;++R.Seen;
    }
}
#endif
