#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGameState.h"
#include "MCMotionRecorder.h"
#include "MCTongue.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void MCTickBrushCoverage(UWorld* World);
void MCTickBrushFacingValidation(UWorld* World);
void MCTickBrushValidation(UWorld* World)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("MCBrushFacing"))) { MCTickBrushFacingValidation(World); return; }
    if(FParse::Param(FCommandLine::Get(),TEXT("MCBrushCoverage"))) { MCTickBrushCoverage(World); return; }
    struct FRun {
        TWeakObjectPtr<UWorld> World; TWeakObjectPtr<AMCToothCharacter> Hero; TWeakObjectPtr<AMCArenaTooth> Tooth;
        TWeakObjectPtr<ACameraActor> Camera; bool Setup=false,Invalid=false; float Age=0,NextFrame=0,LastFrame=-1,MaxError=0,MaxTravel=0,MaxWristStretch=1,MinHandClearance=MAX_flt;
        int32 Seen=0,Frame=0,Contacts=0,PeakFoam=0,ReleasedFoam=-1; uint32 PausedHash=0; FString Timing;
        FVector LastFreeHand=FVector::ZeroVector,LastWorkHand=FVector::ZeroVector; float FreeHandStep=0,WorkHandStep=0; bool HadFreeHand=false;
        TWeakObjectPtr<UMCMotionRecorder> Recorder;
        bool Moving=false,Jumped=false,Air=false; int32 ReturnSamples=0; FVector MotionStart=FVector::ZeroVector; float MotionSpeed=0,ReturnTravel=0,ReachExcess=0;
    };
    static FRun R; if(R.World.Get()!=World) {R=FRun();R.World=World;}
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>(); auto* PC=World->GetFirstPlayerController();
    if(!GS || !PC) return;
    const bool Host=World->GetNetMode()!=NM_Client,Capture=Host && FParse::Param(FCommandLine::Get(),TEXT("MCBrushCapture"));
    const bool Measure=Host && FApp::CanEverRender();
    const bool MovementCase=FParse::Param(FCommandLine::Get(),TEXT("MCBrushMotion"));
    const double Now=GS->GetServerWorldTimeSeconds();
    int32 Expected=1; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
    if(Host) {GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=Now+300;}
    if(Host && !R.Setup && R.Age>3 && GS->PlayerArray.Num()>=Expected) {
#if WITH_EDITOR
        if(Capture && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        AMCArenaTooth* Tooth=nullptr; AMCTongue* Tongue=nullptr;
        for(AMCArenaTooth* T:GS->ArenaTeeth) if(T && T->State.ToothId==3) Tooth=T;
        for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
        auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn()); if(!Tooth || !Tongue || !Hero) return;
        GS->bPhysicalBrushes=false;
        for(AMCArenaTooth* T:GS->ArenaTeeth) if(T) T->SetCoffee(T==Tooth?1:0);
        const FVector Inward=(-Tooth->GetActorLocation()).GetSafeNormal2D(),Side=FVector::CrossProduct(Inward,FVector::UpVector);
        const FVector E=Tooth->Visual->Bounds.BoxExtent;
        const float Radius=FMath::Abs(Inward.X)*E.X+FMath::Abs(Inward.Y)*E.Y;
        bool Found=false; FVector Contact,Normal;
        Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->DisableMovement();
        for(float Gap: {80.f,100.f,120.f}) {
            for(int32 I: {0,-1,1,-2,2,-3,3,-4,4,-5,5,-6,6}) {
                FVector P=Tooth->GetActorLocation()+Inward*(Radius+Gap)+Side*(I*16);
                FHitResult Floor; if(!Tongue->SurfacePoint(P,Floor)) continue; P.Z=Floor.ImpactPoint.Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2;
                Hero->SetActorLocationAndRotation(P,(-Inward).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
                if(Tooth->FindDirtyContact(Hero,Contact,Normal)) {Found=true;break;}
            }
            if(Found) break;
        }
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_FIXTURE found=%d hero=%s contact=%s"),Found,*Hero->GetActorLocation().ToString(),*Contact.ToString());
        if(!Found) { UE_LOG(LogTemp,Error,TEXT("MC_BRUSH_FAIL no reachable visible stain")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        R.Tooth=Tooth; R.Hero=Hero; R.Setup=true; GS->bDevManualEvents=true; GS->DayStartedAt=Now; GS->ForceNetUpdate(); Hero->ForceNetUpdate();
        auto* Recorder=Hero->FindComponentByClass<UMCMotionRecorder>();
        if(!Recorder) { Recorder=NewObject<UMCMotionRecorder>(Hero); Recorder->RegisterComponent(); Recorder->Start(20,TEXT("brush_solo")); }
        R.Recorder=Recorder;
        if(Capture) {
            const FVector Aim=(Contact+Hero->GetActorLocation())*.5+FVector(0,0,15),Eye=Aim+Inward*370+Side*245+FVector(0,0,180);
            auto* Camera=World->SpawnActor<ACameraActor>(); Camera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(49); Camera->GetCameraComponent()->SetAspectRatio(1.5);
            PC->SetViewTarget(Camera); R.Camera=Camera; IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("BrushFrames")),true);
        }
    }
    if(!GS->bDevManualEvents) {if(R.Age>45) FPlatformMisc::RequestExitWithStatus(false,1); return;}
    AMCArenaTooth* Tooth=nullptr; for(AMCArenaTooth* T:GS->ArenaTeeth) if(T && T->State.ToothId==3) Tooth=T;
    if(!Tooth) return;
    const float T=Now-GS->DayStartedAt;
    if(Host && R.Hero.IsValid()) {
        auto* H=R.Hero.Get(); const bool Work=MovementCase?(T>1.5f && T<3.3f):((T>1.5f && T<4.f) || (T>5.5f && T<10.f));
        H->ServerSetPrimary(Work);
        if(R.Recorder.IsValid()) R.Recorder->Stage=T<1.5f?TEXT("idle"):T<4?TEXT("brush"):T<5.5f?TEXT("release"):T<10?TEXT("brush_again"):TEXT("released");
        if(MovementCase && T>3.2f) {
            auto* Move=CastChecked<UMCToothMovementComponent>(H->GetCharacterMovement());
            if(!R.Moving) { R.Moving=true; R.MotionStart=H->GetActorLocation(); Move->SetMovementMode(MOVE_Walking); }
            Move->SetSprinting(T>3.3f && T<6.5f);
            if(T<6.5f) H->AddMovementInput((-Tooth->GetActorLocation()).GetSafeNormal2D());
            if(!R.Jumped && T>3.4f) { H->Jump(); R.Jumped=true; }
            if(T>3.7f) H->StopJumping();
            R.Air|=Move->IsFalling(); R.MotionSpeed=FMath::Max(R.MotionSpeed,float(H->GetVelocity().Size2D()));
            if(R.Recorder.IsValid()) R.Recorder->Stage=T<3.3f?TEXT("brush_walk"):T<4.5f?TEXT("release_jump"):T<6.5f?TEXT("release_sprint"):TEXT("motion_stop");
            if(H->BrushContact->IsPresenting()) {
                ++R.ReturnSamples;
                const auto* Mesh=H->GetMesh(); const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton(); FTransform Rest=FTransform::Identity;
                for(int32 I=Ref.FindBoneIndex(H->RigBone(TEXT("hand_r")));I>=0;I=Ref.GetParentIndex(I)) Rest=Rest*Ref.GetRefBonePose()[I];
                const FVector Offset=Mesh->GetSocketLocation(H->RigBone(TEXT("hand_r")))-Mesh->GetComponentTransform().TransformPosition(Rest.GetLocation());
                R.ReturnTravel=FMath::Max(R.ReturnTravel,float(Offset.Size()));
                R.ReachExcess=FMath::Max(R.ReachExcess,float((Offset-H->BrushContact->ClampHandOffset(Offset)).Size()));
            }
        }
        const FVector Hand=H->GetActorTransform().InverseTransformPosition(H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("hand_l"))));
        const FVector WorkHand=H->GetActorTransform().InverseTransformPosition(H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("hand_r"))));
        if(R.HadFreeHand && T>1 && T<(MovementCase?3.2f:12.f)) {
            R.FreeHandStep=FMath::Max(R.FreeHandStep,float(FVector::Distance(Hand,R.LastFreeHand))/(World->GetDeltaSeconds()*60));
            R.WorkHandStep=FMath::Max(R.WorkHandStep,float(FVector::Distance(WorkHand,R.LastWorkHand))/(World->GetDeltaSeconds()*60));
            if(FVector::Distance(WorkHand,R.LastWorkHand)/(World->GetDeltaSeconds()*60)>10)
                UE_LOG(LogTemp,Warning,TEXT("MC_BRUSH_STEP time=%.3f blend=%.3f work=%d previous=%s current=%s contact=%s normal=%s"),T,H->BrushContact->Alpha(),H->bBrushing,*R.LastWorkHand.ToString(),*WorkHand.ToString(),*H->BrushContact->ContactPoint().ToString(),*H->BrushContact->ContactNormal().ToString());
        }
        R.LastFreeHand=Hand; R.LastWorkHand=WorkHand; R.HadFreeHand=true;
    }
    int32 Changed=0; for(uint8 V:Tooth->GrimeMask) Changed+=V<255;
    const uint32 Hash=FCrc::MemCrc32(Tooth->GrimeMask.GetData(),Tooth->GrimeMask.Num());
    if(T<1.4f && Tooth->Status->State.CoffeeLeft>0 && !Changed) R.Seen|=1;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(It->BrushContact->Target==Tooth && It->BrushContact->IsTouchingSurface() && It->bBrushing) {
        R.Seen|=2;
        if(Measure) {
            const float Error=FVector::Dist(It->BrushContact->BristlePoint(),It->BrushContact->ContactPoint()); R.MaxError=FMath::Max(R.MaxError,Error); ++R.Contacts;
            const auto* Mesh=It->GetMesh(); const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
            auto Rest=[&](FName Role) { FTransform P=FTransform::Identity; for(int32 I=Ref.FindBoneIndex(It->RigBone(Role));I>=0;I=Ref.GetParentIndex(I)) P=P*Ref.GetRefBonePose()[I]; return Mesh->GetComponentTransform().TransformPosition(P.GetLocation()); };
            const FVector Hand=Mesh->GetSocketLocation(It->RigBone(TEXT("hand_r"))),Lower=Mesh->GetSocketLocation(It->RigBone(TEXT("forearm_r")));
            R.MaxTravel=FMath::Max(R.MaxTravel,float(FVector::Dist(Hand,Rest(TEXT("hand_r")))));
            const FVector Offset=Hand-Rest(TEXT("hand_r"));
            R.ReachExcess=FMath::Max(R.ReachExcess,float((Offset-It->BrushContact->ClampHandOffset(Offset)).Size()));
            R.MaxWristStretch=FMath::Max(R.MaxWristStretch,float(FVector::Dist(Hand,Lower)/FVector::Dist(Rest(TEXT("hand_r")),Rest(TEXT("forearm_r")))));
            for(TActorIterator<AMCTongue> Tongue(World);Tongue;++Tongue) { FHitResult Floor; if(Tongue->SurfacePoint(Hand,Floor)) R.MinHandClearance=FMath::Min(R.MinHandClearance,float(Hand.Z-Floor.ImpactPoint.Z)); break; }
        }
    }
    if(Changed>0) R.Seen|=4;
    if(Measure && R.Hero.IsValid()) {
        int32 Particles=0;
        if(auto* Foam=R.Hero->FindComponentByClass<UNiagaraComponent>()) if(auto Controller=Foam->GetSystemInstanceController()) {
            Controller->WaitForConcurrentTickAndFinalize();
            if(auto* System=Controller->GetSystemInstance_Unsafe()) for(const auto& Emitter:System->GetEmitters()) Particles+=Emitter->GetNumParticles();
        }
        R.PeakFoam=FMath::Max(R.PeakFoam,Particles);
        if(T>11.5f) R.ReleasedFoam=Particles;
    }
    if(T>4.5f && T<5.3f) {if(!R.PausedHash) R.PausedHash=Hash; R.Invalid|=R.PausedHash!=Hash; R.Seen|=8;}
    if(Changed>0 && Tooth->Status->State.CoffeeLeft>0) R.Seen|=16;
    if(Capture && T>=R.NextFrame && T<12) {
        R.NextFrame=T+1.f/30;
        if(R.LastFrame>=0) R.Timing+=FString::Printf(TEXT("duration %.6f\n"),T-R.LastFrame);
        R.Timing+=FString::Printf(TEXT("file 'Frame%05d.png'\n"),R.Frame);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("BrushFrames")/FString::Printf(TEXT("Frame%05d.png"),R.Frame++),false,false); R.LastFrame=T;
    }
    if(T>(Host?15:13) || R.Age>60) {
        const bool MotionPass=!MovementCase || (R.ReturnSamples>2 && R.ReturnTravel>30 && R.Air && R.MotionSpeed>470 && FVector::Dist2D(R.MotionStart,R.Hero->GetActorLocation())>300 && R.ReachExcess<=12);
        const bool Pass=MotionPass && R.Seen==31 && !R.Invalid && (!Host || (R.FreeHandStep<1.5f && R.WorkHandStep<10)) && (!Measure || (R.Contacts>20 && R.MaxError<18 && R.ReachExcess<=12 && R.MaxWristStretch<1.05f && R.MinHandClearance>=8 && R.PeakFoam>0 && R.ReleasedFoam==0));
        if(Capture) FFileHelper::SaveStringToFile(R.Timing,*(FPaths::ProjectSavedDir()/TEXT("BrushFrames/times.csv")));
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_%s net=%d seen=%d cells=%d hash=%u contactError=%.2f contacts=%d handTravel=%.2f wristStretch=%.3f groundClearance=%.2f"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),R.Seen,Changed,Hash,R.MaxError,R.Contacts,R.MaxTravel,R.MaxWristStretch,R.MinHandClearance);
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_FREE_HAND maxStep60=%.3f cm (limit 1.5, including start/stop/reacquire)"),R.FreeHandStep);
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_WORK_HAND maxStep60=%.3f cm (limit 10)"),R.WorkHandStep);
        if(Measure) UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_NIAGARA peakParticles=%d releasedParticles=%d"),R.PeakFoam,R.ReleasedFoam);
        if(MovementCase) UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_MOTION pass=%d air=%d speed=%.1f returnTravel=%.2f samples=%d"),MotionPass,R.Air,R.MotionSpeed,R.ReturnTravel,R.ReturnSamples);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
}
#endif
