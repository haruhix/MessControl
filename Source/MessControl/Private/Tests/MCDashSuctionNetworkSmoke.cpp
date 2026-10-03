#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothAnimInstance.h"
#include "MCToothStatusComponent.h"
#include "MCExpressionComponent.h"
#include "MCThroat.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "InputActionValue.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Real owners press/release the same handlers as Shift. Every process observes
// proxy lunges, sustained sprint, distance-weighted intake, drift and recovery.
void MCTickDashSuctionValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ACameraActor> Camera;
        float Age=0;
        bool Setup=false,TapPressed=false,TapReleased=false,HoldPressed=false,HoldReleased=false;
        bool CaptureTapPressed=false,CaptureTapReleased=false;
        bool Placed=false,Delivered=false,Invalid=false,CapturedDash=false,CapturedSuction=false;
        uint8 Dash=0,Sprint=0,Face=0,Drift=0,Quiet=0,Lean=0,PoseRecovery=0;
        FVector Starts[4];
        bool Baseline[4]={};
        float MaxBodyAngle[4]={};
        float MinUpright[4]={1,1,1,1},MinProgress[4]={1,1,1,1},MaxProgress[4]={};
        int32 DashSamples[4]={},LeanSamples[4]={};
        uint8 DashPhaseMask[4]={};
        int32 VideoShot=INDEX_NONE;
        double VideoRecordAt=0;
        bool VideoEpochLogged=false,VideoWideReady=false;
        FVector VideoWideAim,VideoWideOffset;
        float MaxSpeed=0;
    };
    const bool Video=FParse::Param(FCommandLine::Get(),TEXT("MCDashSuctionVideo"));
    static FRun R;if(R.World.Get()!=World) {
        R=FRun();R.World=World;R.VideoRecordAt=World->GetTimeSeconds();
        // Slow the real simulation on every peer, including the listen server.
        // The recorder encodes game timestamps back to ordinary gameplay speed.
        if(Video) {
            float CaptureTimeScale=.25f;
            FParse::Value(FCommandLine::Get(),TEXT("MCCaptureTimeScale="),CaptureTimeScale);
            World->GetWorldSettings()->SetTimeDilation(FMath::Clamp(CaptureTimeScale,.25f,1.f));
            if(World->GetNetMode()!=NM_Client) World->GetWorldSettings()->ForceNetUpdate();
        }
    }
    R.Age+=World->GetDeltaSeconds();
    const bool Host=World->GetNetMode()!=NM_Client;
    const bool Rendered=FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(),TEXT("nullrhi"));
    const bool Capture=Rendered && FParse::Param(FCommandLine::Get(),TEXT("MCDashSuctionCapture"));
    auto* GS=World->GetGameState<AMCGameState>();auto* PC=World->GetFirstPlayerController();
    if(!GS || !PC) {if(R.Age>90) FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const double Now=GS->GetServerWorldTimeSeconds();
    const double T=GS->TasksTotal==6543?Now-GS->DayStartedAt:-1;
    auto Finish=[&]() {
        bool BoundedLean=true;
        for(int32 I=0;I<4;++I) {
            // The lunge must be readable through attack, hold and recovery, and
            // stay upright throughout. A full turn or an unobserved pose fails.
            BoundedLean&=R.MinUpright[I]>.65f && R.MaxBodyAngle[I]<FMath::DegreesToRadians(65.f)
                && R.DashSamples[I]>=3 && R.LeanSamples[I]>=2 && R.DashPhaseMask[I]==7;
            if(Rendered) UE_LOG(LogTemp,Display,TEXT("MC_DASH_POSE net=%d slot=%d peak_tilt_degrees=%.2f peak_body_degrees=%.2f min_up=%.3f phase=%.3f..%.3f phase_mask=%d dash_samples=%d lean_samples=%d leaned=%d recovered=%d"),
                int32(World->GetNetMode()),I,FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(R.MinUpright[I],-1.f,1.f))),
                FMath::RadiansToDegrees(R.MaxBodyAngle[I]),R.MinUpright[I],R.MinProgress[I],R.MaxProgress[I],R.DashPhaseMask[I],R.DashSamples[I],R.LeanSamples[I],
                (R.Lean&(1u<<I))!=0,(R.PoseRecovery&(1u<<I))!=0);
        }
        const bool Pass=!R.Invalid && R.Dash==15 && R.Sprint==15 && R.Face==15 && R.Drift==15 && R.Quiet==15
            && (!Rendered || (R.Lean==15 && R.PoseRecovery==15 && BoundedLean));
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s DASH_SUCTION net=%d dash=%d sprint=%d face=%d drift=%d quiet=%d lean=%d pose_recovery=%d rendered=%d maxSpeed=%.2f invalid=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),R.Dash,R.Sprint,R.Face,R.Drift,R.Quiet,R.Lean,R.PoseRecovery,Rendered,R.MaxSpeed,R.Invalid);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(T>(Host?16.5:15) || R.Age>90) {Finish();return;}
    TArray<AMCToothCharacter*> Heroes;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It) if(It->GetPlayerState()) Heroes.Add(*It);
    Heroes.Sort([](const AMCToothCharacter& A,const AMCToothCharacter& B){return A.GetPlayerState()->GetPlayerId()<B.GetPlayerState()->GetPlayerId();});
    AMCThroat* Throat=nullptr;AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCThroat> It(World);It;++It) {Throat=*It;break;}
    for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
    if(Heroes.Num()!=4 || !Throat || !Tongue) return;
    auto Place=[&](bool DistanceLayout=false) {
        for(int32 I=0;I<4;++I) {
            auto* H=Heroes[I];FHitResult Floor;
            FVector Sample(-600,-600+I*400,0);
            if(Video && DistanceLayout) {
                const FVector Inlet=Throat->VacuumInlet();
                const FVector Inward=Throat->GetActorForwardVector().GetSafeNormal2D();
                const FVector Side=FVector::CrossProduct(FVector::UpVector,Inward);
                Sample=Inlet-Inward*(500+I*600)+Side*((I-1.5f)*140);
                const FVector Zone=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
                const float Clearance=Throat->ZoneRadius+H->GetCapsuleComponent()->GetScaledCapsuleRadius()+120;
                for(int32 Step=0;Step<10 && FVector::DistSquared2D(Sample,Zone)<FMath::Square(Clearance);++Step) Sample-=Inward*100;
            }
            if(!Tongue->SurfacePoint(Sample,Floor)) {R.Invalid=true;continue;}
            H->CancelGameplayInput();H->Status->Initialize(100);
            H->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->SetMovementMode(MOVE_Falling);H->ForceNetUpdate();
            if(Video && DistanceLayout) UE_LOG(LogTemp,Display,TEXT("MC_DASH_SUCTION_VIDEO_POSITION slot=%d distance=%.2f position=%s"),
                I,FVector::Dist2D(H->GetActorLocation(),Throat->VacuumInlet()),*H->GetActorLocation().ToString());
        }
    };
    if(Host && !R.Setup && R.Age>6) {
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            if(!It->Get()->GetPawn() || (!It->Get()->IsLocalController() && It->Get()->AcknowledgedPawn!=It->Get()->GetPawn())) return;
        if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        Tongue->ResetPain();Tongue->ResetPressure();Tongue->bAutomaticYawns=false;Tongue->Settings.bAutomaticJolts=false;Tongue->ForceNetUpdate();
        Throat->ResetSwallow();Throat->AnticipationSeconds=1;Throat->SwallowSeconds=4;
        Place();GS->bDevManualEvents=true;GS->bPhysicalBrushes=false;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;
        GS->TasksTotal=6543;GS->DayStartedAt=Now+1;GS->ForceNetUpdate();R.Setup=true;return;
    }
    if(T<0) return;
    auto* Own=Cast<AMCToothCharacter>(PC->GetPawn());if(!Own) return;
    if(Video && !R.VideoEpochLogged) {
        const double RecordSeconds=World->GetTimeSeconds()-R.VideoRecordAt;
        UE_LOG(LogTemp,Display,TEXT("MC_DASH_VIDEO_START record_time=%.6f scenario_seconds=%.6f record_seconds=%.6f"),RecordSeconds-T,T,RecordSeconds);
        R.VideoEpochLogged=true;
    }
    // Record the production pre-physics poses for the real owner and all proxies.
    // NullRHI remains a gameplay/network test, with no claim about rendered bones.
    if(Rendered) for(auto* H:Heroes) if(auto* Anim=Cast<UMCToothAnimInstance>(H->GetMesh()->GetAnimInstance())) Anim->bRecordMotion=true;
    if(Capture || (Video && Rendered)) {
        if(!R.Camera.IsValid()) {R.Camera=World->SpawnActor<ACameraActor>();PC->SetViewTarget(R.Camera.Get());}
        const int32 Shot=Video?(T<7.75?0:T<11?1:2):(T>8?2:0);
        FVector Aim=Own->GetActorLocation()+FVector(Shot==2?20:0,0,Shot==2?18:8);
        FVector Offset=Shot==2?FVector(245,-120,80):FVector(340,-390,220);
        float FOV=Shot==2?33:38;
        if(Shot==1 && !R.VideoWideReady) {
            // Look along the tongue from inside the front of the mouth. A
            // sphere-fit behind the intake puts this camera inside the palate.
            FBox Bounds(ForceInit);
            for(auto* H:Heroes) {
                Bounds+=H->GetActorLocation()+FVector(-60,-60,-60);
                Bounds+=H->GetActorLocation()+FVector(60,60,90);
            }
            Bounds+=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
            Bounds+=Throat->VacuumInlet();
            Aim=Bounds.GetCenter();Aim.Z=55;
            FOV=65;
            const FBox TongueBounds=Tongue->Surface->Bounds.GetBox();
            const FVector Eye(TongueBounds.Min.X+75,Aim.Y-80,225);
            Offset=Eye-Aim;
            R.VideoWideAim=Aim;R.VideoWideOffset=Offset;R.VideoWideReady=true;
        }
        if(Shot==1) {Aim=R.VideoWideAim;Offset=R.VideoWideOffset;FOV=65;}
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());
        R.Camera->GetCameraComponent()->SetFieldOfView(FOV);
        R.Camera->GetCameraComponent()->SetAspectRatio(Video?16.f/9.f:1.5f);
        if(Video && R.VideoShot!=Shot) {
            const TCHAR* ShotName=Shot==0?TEXT("Dash"):Shot==1?TEXT("WideSuction"):TEXT("FaceSuctionRecovery");
            UE_LOG(LogTemp,Display,TEXT("MC_DASH_SUCTION_VIDEO_SHOT name=%s scenario_seconds=%.6f world_seconds=%.6f record_seconds=%.6f camera=%s aim=%s"),
                ShotName,T,World->GetTimeSeconds(),World->GetTimeSeconds()-R.VideoRecordAt,*R.Camera->GetActorLocation().ToString(),*Aim.ToString());
            R.VideoShot=Shot;
        }
    }
    if(T>=1 && !R.TapPressed) {Own->MoveForward(FInputActionValue(1.f));Own->StartSprint();R.TapPressed=true;}
    if(T>=1.08 && !R.TapReleased) {Own->StopSprint();R.TapReleased=true;}
    if(T>=1 && T<1.65) Own->MoveForward(FInputActionValue(1.f));
    if(T>=1.65 && T<3) Own->MoveForward(FInputActionValue(0.f));
    if(T>=3 && !R.HoldPressed) {Own->MoveForward(FInputActionValue(1.f));Own->StartSprint();R.HoldPressed=true;}
    if(T>=3 && T<4.2) Own->MoveForward(FInputActionValue(1.f));
    if(T>=4.2 && !R.HoldReleased) {Own->StopSprint();Own->MoveForward(FInputActionValue(0.f));R.HoldReleased=true;}
    // PNG readback can block rendering for longer than a short dash. Capture a
    // second real tap after the first dash's uninterrupted pose verification.
    if(Capture && T>=4.75 && !R.CaptureTapPressed) {Own->StartSprint();R.CaptureTapPressed=true;}
    if(Capture && T>=4.83 && !R.CaptureTapReleased) {Own->StopSprint();R.CaptureTapReleased=true;}
    if(Capture && T>4.83 && Own->IsDashing() && !R.CapturedDash && Own->GetDashProgress()>.43f) {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/Unreal_Dash_Tumble.png"),false,false);R.CapturedDash=true;
    }
    if(Host && T>=6 && !R.Placed) {Place(true);R.Placed=true;}
    if(Host && T>=7 && !R.Delivered) {
        const FVector Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
        FMCFoodRow Row;auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};Row.Scale=Row.FragmentScale=FVector(.25);Row.Mass=2;Row.SpoilSeconds=300;
        const FTransform Transform(Center+FVector(0,0,40));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(41);Food->ConfigureItem(TEXT("DashSuctionFixture"),Row,Random,true);Food->FinishSpawning(Transform);
        Food->Body->SetSimulatePhysics(false);R.Invalid|=!Throat->AcceptDelivery(Food);R.Delivered=true;
    }
    for(int32 I=0;I<4;++I) {
        auto* H=Heroes[I];auto* Move=CastChecked<UMCToothMovementComponent>(H->GetCharacterMovement());const uint8 Bit=uint8(1u<<I);
        R.Invalid|=H->GetActorLocation().ContainsNaN() || Move->Velocity.ContainsNaN();
        if(Rendered && T>.8 && T<3) {
            auto* Mesh=H->GetMesh();auto* Anim=Cast<UMCToothAnimInstance>(Mesh->GetAnimInstance());
            const auto* Asset=Mesh->GetSkeletalMeshAsset();
            const int32 Body=Asset?Asset->GetRefSkeleton().FindBoneIndex(H->RigBone(TEXT("body"))):INDEX_NONE;
            if(Anim && Asset && Anim->DiagnosticPose.IsValidIndex(Body)) {
                const auto& Ref=Asset->GetRefSkeleton();FTransform Rest=Ref.GetRefBonePose()[Body];
                for(int32 Parent=Ref.GetParentIndex(Body);Parent>=0;Parent=Ref.GetParentIndex(Parent)) Rest=Rest*Ref.GetRefBonePose()[Parent];
                const FQuat Rotation=Anim->DiagnosticPose[Body].GetRotation();
                const FQuat Delta=Rotation*Rest.GetRotation().Inverse();
                const FVector Up=Anim->DiagnosticMeshUp;
                const float Upright=FVector::DotProduct(Delta.RotateVector(Up),Up);
                const float BodyAngle=FQuat::Identity.AngularDistance(Delta);
                if(Anim->DiagnosticDashProgress>=0) {
                    R.MinUpright[I]=FMath::Min(R.MinUpright[I],Upright);++R.DashSamples[I];
                    R.MaxBodyAngle[I]=FMath::Max(R.MaxBodyAngle[I],BodyAngle);
                    R.MinProgress[I]=FMath::Min(R.MinProgress[I],Anim->DiagnosticDashProgress);R.MaxProgress[I]=FMath::Max(R.MaxProgress[I],Anim->DiagnosticDashProgress);
                    R.DashPhaseMask[I]|=Anim->DiagnosticDashProgress<.25f?1:Anim->DiagnosticDashProgress>.75f?4:2;
                    if(Anim->DiagnosticDashProgress>.15f && Anim->DiagnosticDashProgress<.85f && Upright<.95f) {R.Lean|=Bit;++R.LeanSamples[I];}
                    R.Invalid|=!FMath::IsFinite(Upright) || !FMath::IsFinite(BodyAngle) || Upright<=.65f;
                }
                if(T>2.2 && Anim->DiagnosticDashProgress<0 && (R.Lean&Bit) && Upright>.95f && BodyAngle<FMath::DegreesToRadians(15.f)) R.PoseRecovery|=Bit;
                if(Anim->DiagnosticDashProgress>.15f && Anim->DiagnosticDashProgress<.85f)
                    R.Invalid|=Anim->FootContacts[0].bPlanted || Anim->FootContacts[1].bPlanted;
                R.Invalid|=Anim->DiagnosticPose[Body].ContainsNaN();
            }
        }
        if(T>1 && T<2.8 && H->IsDashing()) {
            R.Dash|=Bit;
        }
        if(T>3.5 && T<4.15) {if(Move->bSprintActive && Move->Velocity.Size2D()>450) R.Sprint|=Bit;R.Invalid|=H->IsDashing();}
        if(T>7.7 && T<8 && !R.Baseline[I]) {R.Starts[I]=H->GetActorLocation();R.Baseline[I]=true;}
        if(T>8.6 && T<11.5) {
            if(Throat->IsAmbientSuctionActive() && H->Expression->FoodSuctionReaction>.03f) R.Face|=Bit;
            const FVector Inward=(Throat->GetSuctionOrigin()-R.Starts[I]).GetSafeNormal2D();
            if(R.Baseline[I] && FVector::DotProduct(H->GetActorLocation()-R.Starts[I],Inward)>2) R.Drift|=Bit;
            R.MaxSpeed=FMath::Max(R.MaxSpeed,float(Move->Velocity.Size2D()));
            R.Invalid|=Move->Velocity.Size2D()>250 || H->SwallowedBy!=nullptr;
            if(Capture && H==Own && !R.CapturedSuction && T>9.5) {
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/Unreal_FoodSuction.png"),false,false);R.CapturedSuction=true;
            }
        }
        if(T>13.5 && !Throat->IsAmbientSuctionActive() && H->Expression->FoodSuctionReaction<.02f) R.Quiet|=Bit;
    }
    if(T>9 && T<11) {
        const FVector Origin=Throat->GetSuctionOrigin();
        R.Invalid|=Throat->GetAmbientSuctionStrengthAt(Origin+FVector(-500,0,0))<=Throat->GetAmbientSuctionStrengthAt(Origin+FVector(-2200,0,0));
    }
}
#endif
