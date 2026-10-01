#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCDevCommands.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsControlComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void MCTickActiveRagdollValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World; TWeakObjectPtr<ACameraActor> Camera;
        double Age=0,NextLog=0; int32 Stage=0,Seen=0,Captured=0,FootSamples=0;
        bool Started=false,Jumped=false,ClientGuard=false,Failed=false;
        float MaxBodyOffset=0,MaxFootDepth=0,BadPoseSeconds=0,NextOffsetDiagnostic=60;
    }; static FRun R;
    if(R.World!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>(); auto* PC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    AMCTongue* Tongue=nullptr; for(TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
    if(!GS || !PC || !H || !Tongue) return;
    const bool Host=World->GetNetMode()!=NM_Client;
    int32 Expected=1; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto Finish=[&](bool Pass) {
        UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_RAGDOLL_%s net=%d seen=%d body_offset=%.2f foot_depth=%.2f foot_samples=%d bad_pose=%.3f client_guard=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),R.Seen,R.MaxBodyOffset,R.MaxFootDepth,R.FootSamples,R.BadPoseSeconds,R.ClientGuard);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(R.Age>100) { Finish(false); return; }
    if(Host && !R.Started && GS->PlayerArray.Num()>=Expected && R.Age>3) {
        World->GetAuthGameMode()->SetActorTickEnabled(false);
        GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
        GS->DayStartedAt=GS->GetServerWorldTimeSeconds()+2; GS->ForceNetUpdate();
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        int32 I=0;
        for(TActorIterator<AMCToothCharacter> It(World);It;++It) {
            FHitResult Floor; const FVector P(-420,(I++-(Expected-1)*.5f)*140,0);
            if(!Tongue->SurfacePoint(P,Floor)) { R.Failed=true; continue; }
            It->GetCharacterMovement()->StopMovementImmediately();
            It->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,61),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            It->GetCharacterMovement()->SetMovementMode(MOVE_Walking); It->Status->Initialize(100);
        }
        R.Started=true;
    }
    if(!Host && GS->bDevManualEvents && !R.Started) {
        const auto Before=H->ToothPhysics->GetActiveRagdollMode(); PC->RequestDevAction(EMCDevAction::ActiveRagdoll,2);
        R.ClientGuard=!PC->CanUseDevPanel() && H->ToothPhysics->GetActiveRagdollMode()==Before; R.Started=true;
    }
    if(!R.Started) return;
    const double T=GS->GetServerWorldTimeSeconds()-GS->DayStartedAt;
    if(T<0) return;
    auto Mode=[&](int32 Value) { PC->RequestDevAction(EMCDevAction::ActiveRagdoll,Value); };
    if(Host) {
        if(R.Stage==0 && T>2) { Mode(1); ++R.Stage; }
        if(R.Stage==1 && T>7) { Mode(2); ++R.Stage; }
        if(R.Stage==2 && T>12) {
            for(TActorIterator<AMCToothCharacter> It(World);It;++It) It->ToothPhysics->ApplyHit(FVector(-300,30,250),It->GetActorLocation());
            ++R.Stage;
        }
        if(R.Stage==3 && T>20) { Mode(0); ++R.Stage; }
    }
    // Each owner uses ordinary CharacterMovement input, including client prediction.
    if(H->ToothPhysics->CanAct() && T>2 && T<11.5) {
        H->AddMovementInput(FVector(FMath::Cos(T*2),FMath::Sin(T*2),0));
        if(T>4 && !R.Jumped) { H->Jump(); R.Jumped=true; }
    }
    if(H->GetCharacterMovement()->IsFalling() && T>4 && T<7) R.Seen|=128;
    if(H->GetVelocity().Size2D()>60 && T>2 && T<11.5) R.Seen|=256;
    if(H->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) R.Seen|=16;
    if(H->ToothPhysics->GetBodyState()==EMCBodyState::Recovering) R.Seen|=32;
    if(T>16 && T<20 && H->ToothPhysics->CanAct()) R.Seen|=64;
    const auto Active=H->ToothPhysics->GetActiveRagdollMode(); R.Seen|=1<<uint8(Active);
    if(H->ToothPhysics->CanAct() && T>1) {
        auto* Mesh=H->GetMesh(); const FVector Body=H->ToothPhysics->PhysicalLocation(); bool Bad=Body.ContainsNaN();
        for(const USkeletalBodySetup* Setup:Mesh->GetPhysicsAsset()->SkeletalBodySetups) {
            const auto* BI=Mesh->GetBodyInstance(Setup->BoneName);
            const FVector P=BI?BI->GetUnrealWorldTransform().GetLocation():Body;
            Bad|=P.ContainsNaN() || FVector::Distance(P,Body)>180;
        }
        R.MaxBodyOffset=FMath::Max(R.MaxBodyOffset,float(FVector::Distance(Body,H->GetActorLocation())));
        if(R.MaxBodyOffset>R.NextOffsetDiagnostic) {
            R.NextOffsetDiagnostic=R.MaxBodyOffset+10;
            const auto* Core=Mesh->GetBodyInstance(H->RigBone(TEXT("body")));
            const FVector Physical=Core?Core->GetUnrealWorldTransform().GetLocation():Body;
            FPhysicsControlTarget Target;
            const FName Set=TEXT("Balance");
            if(auto* Motors=H->FindComponentByClass<UPhysicsControlComponent>()) Motors->GetControlTarget(Motors->GetControlNamesInSet(Set)[0],Target);
            const FVector Goal=H->GetCapsuleComponent()->GetComponentTransform().TransformPosition(Target.TargetPosition);
            const auto* CapsuleBody=H->GetCapsuleComponent()->GetBodyInstance();
            const FVector CapsulePhysics=CapsuleBody?CapsuleBody->GetUnrealWorldTransform().GetLocation():H->GetActorLocation();
            const FVector CapsuleVelocity=CapsuleBody?CapsuleBody->GetUnrealWorldVelocity():FVector::ZeroVector;
            FPhysicsControlData Data; bool Enabled=false;
            if(auto* Motors=H->FindComponentByClass<UPhysicsControlComponent>()) {
                const auto Name=Motors->GetControlNamesInSet(Set)[0]; Motors->GetControlData(Name,Data); Enabled=Motors->GetControlEnabled(Name);
            }
            UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_OFFSET t=%.3f mode=%d cap=%s bone=%s physical=%s goal=%s mesh_relative=%s velocity=%s capsule_physics=%s motor=%d strength=%.1f force=%.1f parent_velocity=%s target_velocity=%s mass=%.2f"),
                T,uint8(Active),*H->GetActorLocation().ToCompactString(),*Body.ToCompactString(),*Physical.ToCompactString(),
                *Goal.ToCompactString(),*Mesh->GetRelativeLocation().ToCompactString(),*H->GetVelocity().ToCompactString(),
                *CapsulePhysics.ToCompactString(),Enabled,Data.LinearStrength,Data.MaxForce,*CapsuleVelocity.ToCompactString(),
                *Target.TargetVelocity.ToCompactString(),Core?Core->GetBodyMass():0);
        }
        if(Active!=EMCActiveRagdollMode::Off && T>3 && T<11.5) {
            const auto* Core=Mesh->GetBodyInstance(H->RigBone(TEXT("body")));
            const auto* Foot=Mesh->GetBodyInstance(H->RigBone(TEXT("foot_l")));
            if(Core && Foot && !Core->IsInstanceSimulatingPhysics() && Foot->IsInstanceSimulatingPhysics() && Core->PhysicsBlendWeight==0 && Foot->PhysicsBlendWeight>.99f) R.Seen|=8;
            for(FName Side:{FName("toe_l"),FName("toe_r")}) {
                const FVector P=Mesh->GetSocketLocation(H->RigBone(Side)); FHitResult Floor;
                if(Tongue->SurfacePoint(P,Floor)) { ++R.FootSamples; R.MaxFootDepth=FMath::Max(R.MaxFootDepth,float(Floor.ImpactPoint.Z-P.Z)); }
            }
        }
        if(Bad) R.BadPoseSeconds+=World->GetDeltaSeconds();
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("MCActiveRagdollCapture")) && Host) {
        if(!R.Camera.IsValid()) {
            R.Camera=World->SpawnActor<ACameraActor>(); R.Camera->GetCameraComponent()->SetFieldOfView(48); PC->SetViewTarget(R.Camera.Get());
        }
        const FVector Aim=H->GetActorLocation()+FVector(0,0,10),Offset(320,-360,230);
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());
        const double Times[]={1.5,5.5,10,12.35,14.6,18,22};
        if(R.Captured<7 && T>=Times[R.Captured]) {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/FString::Printf(TEXT("Artifacts/ActiveRagdoll/%02d.png"),R.Captured++),true,false);
        }
    }
    if(R.Age>=R.NextLog) {
        R.NextLog=R.Age+3;
        UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_RAGDOLL net=%d t=%.2f mode=%d seen=%d offset=%.2f depth=%.2f bad=%.3f"),
            int32(World->GetNetMode()),T,uint8(Active),R.Seen,R.MaxBodyOffset,R.MaxFootDepth,R.BadPoseSeconds);
    }
    if(T>(Host?27:24)) Finish(!R.Failed && R.Seen==511 && R.BadPoseSeconds<.05f && R.MaxBodyOffset<100 && R.FootSamples>60 && R.MaxFootDepth<18 && (Host || R.ClientGuard));
}
#endif
