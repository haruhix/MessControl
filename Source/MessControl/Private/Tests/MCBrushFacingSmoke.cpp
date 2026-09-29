#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

void MCTickBrushFacingValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCArenaTooth> Tooth;
        TWeakObjectPtr<AMCMouthSurface> Patch;
        TWeakObjectPtr<ACameraActor> Camera;
        float Age=0,At=0,MinFacing=1,TurnError=180;
        int32 Stage=-1,Contacts=0,Passed=0;
        uint32 BehindHash=0,AwayHash=0;
        bool Turned=false,Restored=false,Away=false,Shot=false,Invalid=false;
        FVector Facing=FVector::ForwardVector,Start=FVector::ZeroVector;
    };
    static FRun R; if(R.World!=World) {R=FRun();R.World=World;}
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>();auto* PC=World->GetFirstPlayerController();
    auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    if(!H || !GS || R.Age<3) return;
    GS->Phase=EMCShiftPhase::Intermission;GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;GS->bPhysicalBrushes=false;
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(World);It;++It){Tongue=*It;break;}
    if(!Tongue)return;
    if(R.Stage<0 || R.Age-R.At>7) {
        if(R.Stage>=0) {
            const bool Pass=!R.Invalid && R.Contacts>10 && R.TurnError<12 && R.MinFacing>=.85f
                && FVector::Dist2D(R.Start,H->GetActorLocation())>80;
            R.Passed+=Pass;
            UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_FACING_CASE stage=%d pass=%d contacts=%d minFacing=%.3f turnError=%.1f retreat=%.1f"),R.Stage,Pass,R.Contacts,R.MinFacing,R.TurnError,FVector::Dist2D(R.Start,H->GetActorLocation()));
        }
        H->ServerSetPrimary(false);H->ResetContact();
        if(R.Patch.IsValid())R.Patch->Destroy();R.Patch=nullptr;R.Tooth=nullptr;
        if(++R.Stage>=3) {
            UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_FACING_%s cases=%d/3"),R.Passed==3?TEXT("PASS"):TEXT("FAIL"),R.Passed);
            FPlatformMisc::RequestExitWithStatus(false,R.Passed==3?0:1);return;
        }
        R.At=R.Age;R.Contacts=0;R.MinFacing=1;R.TurnError=180;R.Invalid=false;R.Turned=false;R.Restored=false;R.Away=false;R.Shot=false;
        H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->DisableMovement();
        for(AMCArenaTooth* T:GS->ArenaTeeth)if(T)T->SetCoffee(0);
        if(R.Stage==0) {for(TActorIterator<AMCMouthSurface> It(World);It;++It)It->Destroy();}
        FVector Contact,Normal;bool Found=false;
        if(R.Stage<2) {
            for(AMCArenaTooth* T:GS->ArenaTeeth)if(T && T->State.ToothId==3+R.Stage)R.Tooth=T;
            if(R.Tooth.IsValid()) {
                auto* T=R.Tooth.Get();T->SetCoffee(1);
                const FVector Inward=(-T->GetActorLocation()).GetSafeNormal2D(),Side=FVector::CrossProduct(Inward,FVector::UpVector);
                const FVector E=T->Visual->Bounds.BoxExtent;const float Radius=FMath::Abs(Inward.X)*E.X+FMath::Abs(Inward.Y)*E.Y;
                for(float Gap:{80.f,100.f,120.f}) {
                    for(int32 I:{0,-1,1,-2,2,-3,3,-4,4}) {
                        FVector P=T->GetActorLocation()+Inward*(Radius+Gap)+Side*(I*16);FHitResult Floor;
                        if(!Tongue->SurfacePoint(P,Floor))continue;
                        P.Z=Floor.ImpactPoint.Z+H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2;
                        H->SetActorLocationAndRotation(P,(-Inward).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
                        if(T->FindDirtyContact(H,Contact,Normal)){Found=true;break;}
                    }
                    if(Found)break;
                }
            }
        } else {
            FHitResult Floor;const FVector Center(-430,0,0);
            if(Tongue->SurfacePoint(Center,Floor)) {
                auto* Patch=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),FTransform(Floor.ImpactPoint+Floor.ImpactNormal*3));
                Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=65;Patch->GroundResponse=EMCGroundSurface::Normal;
                Patch->FinishSpawning(FTransform(Floor.ImpactPoint+Floor.ImpactNormal*3));Patch->Status->ApplyCoffee();R.Patch=Patch;
                FVector P=Center-FVector(95,0,0);Tongue->SurfacePoint(P,Floor);P.Z=Floor.ImpactPoint.Z+H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2;
                H->SetActorLocationAndRotation(P,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
                Found=Patch->FindDirtyContact(H,Contact,Normal);
            }
        }
        if(!Found){UE_LOG(LogTemp,Error,TEXT("MC_BRUSH_FACING_FAIL no fixture stage=%d"),R.Stage);FPlatformMisc::RequestExitWithStatus(false,1);return;}
        R.Facing=(Contact-H->GetActorLocation()).GetSafeNormal2D();R.Start=H->GetActorLocation();
        H->SetActorRotation(R.Facing.Rotation()+FRotator(0,45,0));H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        if(FParse::Param(FCommandLine::Get(),TEXT("MCBrushCapture"))) {
            if(!R.Camera.IsValid()){R.Camera=World->SpawnActor<ACameraActor>();R.Camera->GetCameraComponent()->SetFieldOfView(54);PC->SetViewTarget(R.Camera.Get());}
            const FVector Aim=(R.Start+Contact)*.5+FVector(0,0,20),Eye=Aim-R.Facing*320+FVector::CrossProduct(R.Facing,FVector::UpVector)*250+FVector(0,0,190);
            R.Camera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
        }
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_FACING_FIXTURE stage=%d pawn=%s point=%s"),R.Stage,*R.Start.ToString(),*Contact.ToString());
    }
    const float T=R.Age-R.At;H->ServerSetPrimary(T>.5f);
    const auto& Mask=R.Tooth.IsValid()?R.Tooth->GrimeMask:R.Patch->WipeMask;
    const uint32 Hash=FCrc::MemCrc32(Mask.GetData(),Mask.Num());
    auto* Brush=H->BrushContact.Get();
    if(Brush->IsTouchingSurface() && H->bBrushing) {
        ++R.Contacts;R.MinFacing=FMath::Min(R.MinFacing,float(FVector::DotProduct(H->GetActorForwardVector(),(Brush->ContactPoint()-H->GetActorLocation()).GetSafeNormal2D())));
        if(T>1 && T<2.6)R.TurnError=FMath::Min(R.TurnError,float(FMath::Abs(FMath::FindDeltaAngleDegrees(H->GetActorRotation().Yaw,(Brush->ContactPoint()-H->GetActorLocation()).Rotation().Yaw))));
    }
    if(T>2.6f && !R.Turned) {R.Turned=true;R.BehindHash=Hash;H->SetActorRotation(R.Facing.Rotation()+FRotator(0,180,0));}
    if(T>2.7f && T<3.3f)R.Invalid|=Hash!=R.BehindHash || Brush->IsTouchingSurface();
    if(T>3.3f && !R.Restored){R.Restored=true;H->SetActorRotation(R.Facing.Rotation());}
    if(T>5)H->AddMovementInput(-R.Facing);
    if(T>5.2f && !R.Away){R.Away=true;R.AwayHash=Hash;}
    if(T>5.3f)R.Invalid|=Hash!=R.AwayHash || Brush->IsTouchingSurface();
    if(T>2.2f && !R.Shot && R.Camera.IsValid()) {
        const FString Folder=FPaths::ProjectDir()/TEXT("Artifacts/BrushFacing");IFileManager::Get().MakeDirectory(*Folder,true);
        FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("Facing%02d.png"),R.Stage),false,false);R.Shot=true;
    }
}
#endif
