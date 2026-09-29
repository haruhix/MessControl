#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void MCTickCameraValidation(UWorld* World)
{
    struct FRun {TWeakObjectPtr<UWorld> World;float Age=0,StageAt=0;int32 Stage=-1,Seen=0;bool Invalid=false,Shot=false;};
    static FRun R;if(R.World!=World){R=FRun();R.World=World;}
    R.Age+=World->GetDeltaSeconds();
    auto* PC=World->GetFirstPlayerController();auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=World->GetGameState<AMCGameState>();
    if(!H || !GS || R.Age<3)return;
    GS->Phase=EMCShiftPhase::Intermission;GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    const FVector Places[]={FVector(-400,-800,0),FVector(-40,-820,0),FVector(500,-730,0),FVector(-400,780,0),FVector(500,720,0),FVector(900,0,0),FVector(1100,0,330)};
    if(R.Stage<0 || R.Age-R.StageAt>1.8f){
        ++R.Stage;R.StageAt=R.Age;R.Shot=false;
        if(R.Stage>=UE_ARRAY_COUNT(Places)){
            const bool Pass=!R.Invalid && R.Seen==UE_ARRAY_COUNT(Places);
            UE_LOG(LogTemp,Display,TEXT("MC_CAMERA_%s viewpoints=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),R.Seen);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
        }
        FVector P=Places[R.Stage];
        if(R.Stage<6)for(TActorIterator<AMCTongue> It(World);It;++It){FHitResult Hit;if(It->SurfacePoint(P,Hit))P.Z=Hit.ImpactPoint.Z+H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2;break;}
        H->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
        H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->DisableMovement();
    }
    if(!R.Shot && R.Age-R.StageAt>1.5f){
        const FVector Eye=H->Camera->GetComponentLocation();bool Blocked=false;
        for(TActorIterator<AActor> It(World);It;++It)if(auto* C=It->FindComponentByClass<UStaticMeshComponent>();C && C->GetStaticMesh() && C->GetStaticMesh()->GetName()==TEXT("SM_SoftPalate")){
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCCameraShell),true,H);
            Blocked|=C->LineTraceComponent(Hit,H->GetActorLocation()+FVector(0,0,75),Eye,Q);
        }
        const float Facing=FVector::DotProduct(H->Camera->GetForwardVector(),(H->GetActorLocation()-Eye).GetSafeNormal());
        R.Invalid|=Blocked || Eye.ContainsNaN() || Facing<.85f || !H->CameraBoom->bDoCollisionTest;
        UE_LOG(LogTemp,Display,TEXT("MC_CAMERA_VIEW %d blocked=%d facing=%.3f eye=%s"),R.Stage,Blocked,Facing,*Eye.ToString());
        if(FParse::Param(FCommandLine::Get(),TEXT("MCCameraCapture"))){
            const FString Folder=FPaths::ProjectDir()/TEXT("Artifacts/Camera");IFileManager::Get().MakeDirectory(*Folder,true);
            FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("View%02d.png"),R.Stage),true,false);
        }
        R.Shot=true;++R.Seen;
    }
}
#endif
